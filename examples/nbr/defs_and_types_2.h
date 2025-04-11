#define NODE_A_ID 27651 // sensor #15
#define NODE_B_ID 5489 // sensor #16

// Configures the wake-up timer for neighbour discovery
#define WAKE_TIME RTIMER_SECOND/10    // 10 HZ, 0.1s
#define SLEEP_SLOT RTIMER_SECOND/10   // sleep slot should not be too large to prevent overflow
#define E_WAKE_TIME CLOCK_SECOND/10    // 10 HZ, 0.1s
#define E_SLEEP_SLOT CLOCK_SECOND/10
#define NUM_SEND 2

#define MAX_NUM_DATA 60

#define MOTION_FREQUENCY 10
#define MOTION_THRESHOLD 2

typedef struct {
	unsigned short src_id;
	unsigned short last_discovered_node_id;
} nbr_packet_struct;

typedef struct {
	double light;
	double motion;
} data_tuple_struct;

typedef struct {
	unsigned long src_id;
	unsigned long seq;
	data_tuple_struct data_tuple;
} data_packet_struct;

typedef struct {
	unsigned long src_id;
	unsigned long seq;
} ack_packet_struct;