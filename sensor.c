#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/neutrino.h>
#include <sys/dispatch.h>
#include <devctl.h>
#include <hw/i2c.h>
#include <time.h>
#include <errno.h>
#include <string.h>
#include <math.h>
#include <curl/curl.h>

#define SENSOR_MSG_TYPE 1
#define BME280_ADDR 0x77
#define I2C_DEVICE "/dev/i2c1"

typedef struct {
	int type;
	float temperature;
	float humidity;
} sensor_data_t;

// Temperature
static uint16_t dig_T1;
static int16_t dig_T2;
static int16_t dig_T3;
static int32_t t_fine;

//Humidity
static uint8_t dig_H1;
static int16_t dig_H2;
static uint8_t dig_H3;
static int16_t dig_H4;
static int16_t dig_H5;
static int8_t dig_H6;

int bme280_write_reg(int fd, uint8_t reg, uint8_t val){
	size_t bufsize = sizeof(i2c_send_t) + 2;
	uint8_t *buffer = malloc(bufsize);
	if (!buffer){
		perror("malloc()");
		return -1;
	}
	i2c_send_t *hdr = (i2c_send_t *)buffer;
	hdr->slave.addr = BME280_ADDR;
	hdr->slave.fmt = I2C_ADDRFMT_7BIT;
	hdr->len = 2;
	hdr->stop = 1;

	buffer[sizeof(i2c_send_t)] = reg;
	buffer[sizeof(i2c_send_t)+1] = val;

	if (devctl(fd,DCMD_I2C_SEND,buffer,bufsize,NULL) < 0){
		perror("bme280_write_reg()");
		free(buffer);
		return -1;
	}
	free(buffer);
	return 0;
}

int bme280_read_regs(int fd, uint8_t reg, uint8_t *buf, size_t len){
	size_t bufsize = sizeof(i2c_sendrecv_t) + 1 + len;
	uint8_t *buffer = malloc(bufsize);
	if (!buffer){
		perror("malloc()");
		return -1;
	}
	i2c_sendrecv_t *hdr = (i2c_sendrecv_t *)buffer;
	hdr->slave.addr = BME280_ADDR;
	hdr->slave.fmt = I2C_ADDRFMT_7BIT;
	hdr->send_len = 1;
	hdr->recv_len = len;
	hdr->stop = 0;

	buffer[sizeof(i2c_sendrecv_t)] = reg;

	if (devctl(fd,DCMD_I2C_SENDRECV,buffer,bufsize,NULL) < 0){
		perror("bme280_read_reg()");
		free(buffer);
		return -1;
	}
	memcpy(buf,buffer + sizeof(i2c_sendrecv_t) + 1, len);
	free(buffer);
	return 0;
}

int bme280_read_temp_calibrate(int fd){
	uint8_t calib[6];

	if (bme280_read_regs(fd, 0x88, calib, sizeof(calib)) < 0){
		return -1;
	}
	dig_T1 = (uint16_t)(calib[0] | (calib[1] << 8));
	dig_T2 = (int16_t)(calib[2] | (calib[3] << 8));
	dig_T3 = (int16_t)(calib[4] | (calib[5] << 8));
	return 0;
}

int bme280_read_hum_calibrate(int fd){
	if (bme280_read_regs(fd, 0xA1, &dig_H1, 1) < 0){
		return -1;
	}
	uint8_t hum_calib[7];

	if (bme280_read_regs(fd, 0xE1, hum_calib, 7) < 0){
		return -1;
	}

	dig_H2 = (int16_t)(hum_calib[0] | (hum_calib[1] << 8));
	dig_H3 = hum_calib[2];

	dig_H4 = (int16_t)(((int8_t)hum_calib[3] << 4) | (hum_calib[4] & 0x0F));
	dig_H5 = (int16_t)(((int8_t)hum_calib[5] << 4) | (hum_calib[4] >> 4));
	dig_H6 = (int8_t)hum_calib[6];
	return 0;
}

int bme280_init(int fd){
	if (bme280_write_reg(fd, 0xF2, 0x01) < 0){
		return -1;
	}
	usleep(10000);

	if (bme280_write_reg(fd, 0xF4, 0x27) < 0){
		fprintf(stderr, "Failed to write\n");
		return -1;
	}
	usleep(10000);
	return 0;
}

float calc_temp(uint32_t adc_T){
	int32_t var1, var2;
	var1 = (((adc_T >> 3) - ((int32_t)dig_T1 << 1)) * (int32_t)dig_T2) >> 11;
	var2 = (((((adc_T >> 4) - (int32_t)dig_T1) * ((adc_T >> 4) - (int32_t)dig_T1)) >> 12) * (int32_t)dig_T3) >> 14;
	t_fine = var1 + var2;
	int32_t temp = (t_fine * 5 + 128) >> 8;
	return temp / 100.0f;
}

float calc_humd(uint32_t adc_H){
	int32_t var1;
	var1 = t_fine - 76800;
	var1 = (((((adc_H << 14) - ((int32_t)dig_H4 << 20) - (((int32_t)dig_H5) * var1)) + 16384) >> 15) *
            (((((((var1 * ((int32_t)dig_H6)) >> 10) *
                 (((var1 * ((int32_t)dig_H3)) >> 11) + 32768)) >> 10) + 2097152) *
             ((int32_t)dig_H2) + 8192) >> 14));
	var1 = var1 - (((((var1 >> 15) * (var1 >> 15)) >> 7) *
            ((int32_t)dig_H1)) >> 4);

	if (var1 < 0)
		var1 = 0;
	if (var1 > 419430400)
		var1 = 419430400;
	float hum = (var1 >> 12);
	return (hum / 1024.0f) * 10;
}

void send_sensor_data_post(int temp){
	CURL *curl;
	CURLcode res;
	char post_data[256];
	curl = curl_easy_init();
	if (curl) {
		//curl_easy_setopt(curl,CURLOPT_URL, "<localip>/api/temperatures");
		snprintf(post_data,sizeof(post_data),"temperature=%2d",temp);
		curl_easy_setopt(curl,CURLOPT_POSTFIELDS,post_data);

		res = curl_easy_perform(curl);
		if (res != CURLE_OK){
			fprintf(stderr, "POST request failed: %s\n", curl_easy_strerror(res));
		}
		curl_easy_cleanup(curl);
	}
}

int main(int argc, char *argv[]){
	int fd, coid;
	sensor_data_t sensor_data;

	fd = open(I2C_DEVICE, O_RDWR);
	if (fd < 0){
		perror("Failed to open I2C device");
		exit(EXIT_FAILURE);
	}

	if (bme280_init(fd) < 0){
		fprintf(stderr, "BME280 initialization failed\n");
		close(fd);
		exit(EXIT_FAILURE);
	}

	if (bme280_read_temp_calibrate(fd) < 0){
		fprintf(stderr, "BME280 temperature calibration failed\n");
		close(fd);
		exit(EXIT_FAILURE);
	}

	if (bme280_read_hum_calibrate(fd) < 0){
		fprintf(stderr, "BME280 humidity calibration failed\n");
		close(fd);
		exit(EXIT_FAILURE);
	}

	if ((coid = name_open("sensor_server",0)) == -1){
		perror("name_open()");
		close(fd);
		exit(EXIT_FAILURE);
	}

	while (1){
		uint8_t temp_raw[3];
		uint8_t hum_raw[2];
		uint32_t adc_T;
		uint16_t adc_H;

		if (bme280_read_regs(fd,0xFA,temp_raw,3) < 0){
			fprintf(stderr, "BME280 temperature reading failed\n");
			break;
		}

		if (bme280_read_regs(fd,0xFD,hum_raw,2) < 0){
			fprintf(stderr, "BME280 humidity reading failed\n");
			break;
		}

		adc_T = ((uint32_t)temp_raw[0] << 12) | ((uint32_t)temp_raw[1] << 4) | (temp_raw[2] >> 4);
		adc_H = ((uint16_t)hum_raw[0] << 8) | hum_raw[1];

		sensor_data.temperature = calc_temp(adc_T);
		sensor_data.humidity = calc_humd(adc_H);
		sensor_data.type = SENSOR_MSG_TYPE;

		int temp_int = (int)roundf(sensor_data.temperature);
		printf("Sensor reading: Temperature: %2d°C, Humidity %.2f%%\n", temp_int, sensor_data.humidity);

		if (MsgSend(coid,&sensor_data, sizeof(sensor_data), NULL, 0) < 0){
			perror("MsgSend()");
		}
		/*
		if (sensor_data.temperature > 35.0f) {
			int pulseCode = 1;
			MsgSendPulse(coid,SIGEV_PULSE_PRIO_INHERIT,pulseCode,0);
		}*/
		//send_sensor_data_post(temp_int);
		sleep(6);

	}
	name_close(coid);
	close(fd);
	return 0;
}


