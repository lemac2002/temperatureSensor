#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/neutrino.h>
#include <sys/dispatch.h>
#include <string.h>
#include <math.h>

#define SENSOR_MSG_TYPE 1
#define ATTACH_POINT "sensor_server"

typedef struct {
	int type;
	float temperature;
	float humidity;
} sensor_data_t;

int main(int arc, char *argvp[]){
	name_attach_t *attach;
	int rcvid;
	sensor_data_t sensor_data;

	attach = name_attach(NULL, ATTACH_POINT,0);
		if (attach == NULL){
			perror("name_attach()");
			return EXIT_FAILURE;
		}

	printf("Started. Waiting for input...\n");

	while (1){
		rcvid = MsgReceive(attach->chid, &sensor_data,sizeof(sensor_data),NULL);
		if (rcvid == -1){
			perror("MsgReceive()");
			break;
		}
		if (rcvid == 0){
			perror("Received alert.\n");
			continue;
		}

		if (sensor_data.type == SENSOR_MSG_TYPE){
			int temp_int = (int)roundf(sensor_data.temperature);
			printf("Received sensor data: Temperature: %2d°C, Humidity %.2f%%\n", temp_int, sensor_data.humidity);
		}
		MsgReply(rcvid,0,NULL,0);

	}
	name_detach(attach,0);
	return 0;
}
