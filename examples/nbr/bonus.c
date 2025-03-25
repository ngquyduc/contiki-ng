/*
 * CS4222/5422: Wireless Networking
 * Task 2 with Bonus: Motion-Triggered Delay-Tolerant Backhauling Network
 * 
 * Enhanced implementation for the bonus task:
 * - Node A starts sensing only when significant motion is detected
 * - Upon motion detection, captures data for 60 seconds
 * - Waits for further motion events to restart sensing
 * - Data transfer occurs only when both nodes are stationary for 1 minute
 */

#include "contiki.h"
#include "net/netstack.h"
#include "net/nullnet/nullnet.h"
#include "net/packetbuf.h"
#include "lib/random.h"
#include "net/linkaddr.h"
#include <string.h>
#include <stdio.h>
#include "node-id.h"
#include "board-peripherals.h"

// Identification information
#define NODE_A_ID 1  // Change these IDs based on your actual node IDs
#define NODE_B_ID 2

// Configures the wake-up timer for neighbor discovery 
#define WAKE_TIME (RTIMER_SECOND/8)    // 125ms wake time 
#define MAX_SLEEP_CYCLE 6              // Maximum sleep cycle 
#define MIN_SLEEP_CYCLE 2              // Minimum sleep cycle
#define SLEEP_SLOT (RTIMER_SECOND/10)  // 100ms sleep slot
#define NUM_SEND 3                     // Number of packets during wake time

// Constants for data collection
#define SENSING_INTERVAL (CLOCK_SECOND) // 1Hz sensing rate
#define MAX_READINGS 60                 // Store 60 seconds of data
#define RSSI_THRESHOLD -75              // RSSI threshold for "good" link quality
#define RSSI_SAMPLES 5                  // Number of samples to average for link quality

// Motion detection thresholds
#define MOTION_DETECTION_THRESHOLD 50    // Threshold for significant motion
#define MOTION_CHECK_INTERVAL (CLOCK_SECOND/2) // Check for motion every 0.5 seconds
#define STATIONARY_THRESHOLD 20          // Threshold for stationary detection
#define STATIONARY_TIME (CLOCK_SECOND*60) // 60 seconds of being stationary

// For neighbor discovery, we use the broadcast address
linkaddr_t dest_addr;

// Data packet types
enum {
	PACKET_TYPE_DISCOVERY = 0,
	PACKET_TYPE_DATA_REQUEST = 1,
	PACKET_TYPE_DATA_RESPONSE = 2,
	PACKET_TYPE_MOTION_STATUS = 3   // New packet type for motion status
};

// Motion states
enum {
	MOTION_STATE_ACTIVE = 0,
	MOTION_STATE_STATIONARY = 1
};

// Structure for discovery packets
typedef struct {
	uint8_t type;            // Packet type
	uint16_t src_id;         // Source node ID
	uint16_t last_heard_id;  // ID of the last node heard from
	int16_t seq;             // Sequence number
	uint8_t motion_state;    // Current motion state
} discovery_packet_t;

// Structure for data request packets
typedef struct {
	uint8_t type;            // Packet type
	uint16_t src_id;         // Source node ID
	uint16_t last_index;     // Last index received
} data_request_packet_t;

// Structure for sensor data packets
typedef struct {
	uint8_t type;            // Packet type
	uint16_t src_id;         // Source node ID
	uint16_t start_index;    // Starting index of readings
	uint8_t num_readings;    // Number of readings in this packet
	uint16_t light_readings[10];  // Can send up to 10 readings per packet
	uint16_t motion_readings[10]; // Motion readings corresponding to light readings
} data_packet_t;

// Structure for motion status packet
typedef struct {
	uint8_t type;            // Packet type
	uint16_t src_id;         // Source node ID
	uint8_t motion_state;    // Current motion state
} motion_status_packet_t;

// Sensor data storage
static uint16_t light_readings[MAX_READINGS];
static uint16_t motion_readings[MAX_READINGS];
static uint16_t current_reading_index = 0;
static uint16_t last_sent_index = 0;
static uint8_t data_collection_complete = 0;
static uint8_t data_collection_active = 0;

// RSSI history for link quality assessment
static int16_t rssi_history[RSSI_SAMPLES];
static uint8_t rssi_index = 0;
static int16_t rssi_sum = 0;
static uint8_t rssi_count = 0;

// Discovery state variables
static uint8_t discovered_nodes[256] = {0};
static uint16_t last_heard_node_id = 0;
static int16_t discovery_seq = 0;
static uint8_t ready_to_transfer = 0;

// Motion detection variables
static uint8_t motion_state = MOTION_STATE_ACTIVE;
static uint8_t remote_node_motion_state = MOTION_STATE_ACTIVE;
static uint16_t stationary_time_counter = 0;
static int16_t last_accel_readings[3] = {0, 0, 0};
static uint16_t stationary_counter = 0;

// Timers
static struct rtimer rt;
static struct etimer sensing_timer;
static struct etimer motion_check_timer;
static struct etimer stationary_timer;
static struct etimer transfer_timer;

// Protothread
static struct pt pt;

// Process definitions
PROCESS(bonus, "bonus task");
AUTOSTART_PROCESSES(&bonus);

// Function to read the accelerometer for motion detection
uint8_t check_significant_motion(void) {
	int16_t x, y, z;
	int16_t delta_x, delta_y, delta_z;
	uint16_t motion_magnitude;

	// Read current accelerometer values
	x = adxl362_read_x();
	y = adxl362_read_y();
	z = adxl362_read_z();

	// Calculate change from last reading
	delta_x = x - last_accel_readings[0];
	delta_y = y - last_accel_readings[1];
	delta_z = z - last_accel_readings[2];

	// Save current readings for next comparison
	last_accel_readings[0] = x;
	last_accel_readings[1] = y;
	last_accel_readings[2] = z;

	// Calculate magnitude of motion (approximate, using absolute values)
	motion_magnitude = abs(delta_x) + abs(delta_y) + abs(delta_z);

	// Return 1 if significant motion detected, 0 otherwise
	return motion_magnitude > MOTION_DETECTION_THRESHOLD;
}

// Function to check if device is stationary
uint8_t check_if_stationary(void) {
	int16_t x, y, z;
	int16_t delta_x, delta_y, delta_z;
	uint16_t motion_magnitude;

	// Read current accelerometer values
	x = adxl362_read_x();
	y = adxl362_read_y();
	z = adxl362_read_z();

	// Calculate change from last reading
	delta_x = x - last_accel_readings[0];
	delta_y = y - last_accel_readings[1];
	delta_z = z - last_accel_readings[2];

	// Save current readings for next comparison
	last_accel_readings[0] = x;
	last_accel_readings[1] = y;
	last_accel_readings[2] = z;

	// Calculate magnitude of motion (approximate, using absolute values)
	motion_magnitude = abs(delta_x) + abs(delta_y) + abs(delta_z);

	// Return 1 if stationary (motion below threshold), 0 otherwise
	return motion_magnitude < STATIONARY_THRESHOLD;
}

// Function to update motion state
void update_motion_state(void) {
	uint8_t is_stationary = check_if_stationary();

	if (is_stationary) {
		stationary_counter++;
		if (stationary_counter >= (STATIONARY_TIME / MOTION_CHECK_INTERVAL)) {
			// Device has been stationary for the required time
			if (motion_state != MOTION_STATE_STATIONARY) {
				motion_state = MOTION_STATE_STATIONARY;
				printf("\nDevice is now STATIONARY");
			}
		}
	} else {
		// Reset counter if motion detected
		stationary_counter = 0;
		if (motion_state != MOTION_STATE_ACTIVE) {
			motion_state = MOTION_STATE_ACTIVE;
			printf("\nDevice is now ACTIVE");
		}
	}
}

// Function to read the light sensor
uint16_t read_light_sensor(void) {
	uint16_t light_value;

	SENSORS_ACTIVATE(opt_3001_sensor);
	// Give some time for the sensor to warm up
	etimer_set(&sensing_timer, CLOCK_SECOND / 10);
	PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&sensing_timer));

	light_value = opt_3001_sensor.value(0);
	SENSORS_DEACTIVATE(opt_3001_sensor);

	return light_value;
}

// Function to read the motion sensor
uint16_t read_motion_sensor(void) {
	uint16_t motion_value;

	SENSORS_ACTIVATE(motion_sensor);
	// Give some time for the sensor to warm up
	etimer_set(&sensing_timer, CLOCK_SECOND / 10);
	PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&sensing_timer));

	motion_value = motion_sensor.value(0);
	SENSORS_DEACTIVATE(motion_sensor);

	return motion_value;
}

// Function to calculate average RSSI
int16_t get_average_rssi(void) {
	if (rssi_count == 0) return -127; // No samples yet
	return rssi_sum / rssi_count;
}

// Function to determine if link quality is good
uint8_t is_link_quality_good(void) {
	int16_t avg_rssi = get_average_rssi();
	return (avg_rssi >= RSSI_THRESHOLD && rssi_count >= RSSI_SAMPLES);
}

// Function to add an RSSI sample
void add_rssi_sample(int16_t rssi) {
	// If buffer is full, remove oldest sample
	if (rssi_count == RSSI_SAMPLES) {
		rssi_sum -= rssi_history[rssi_index];
		rssi_count--;
	}

	// Add new sample
	rssi_history[rssi_index] = rssi;
	rssi_sum += rssi;
	rssi_count++;

	// Update index for next sample
	rssi_index = (rssi_index + 1) % RSSI_SAMPLES;
}

// Function to reset RSSI history
void reset_rssi_history(void) {
	rssi_index = 0;
	rssi_sum = 0;
	rssi_count = 0;
}

// Callback for received packets
void receive_packet_callback(const void *data, uint16_t len, const linkaddr_t *src, const linkaddr_t *dest) {
	// Get RSSI from the received packet
	int16_t rssi = (int16_t)packetbuf_attr(PACKETBUF_ATTR_RSSI);

	// Store RSSI sample
	add_rssi_sample(rssi);

	// Check packet type
	uint8_t packet_type = ((uint8_t*)data)[0];

	switch (packet_type) {
		case PACKET_TYPE_DISCOVERY: {
						    if (len == sizeof(discovery_packet_t)) {
							    discovery_packet_t *packet = (discovery_packet_t*)data;

							    // Record that we discovered this node
							    discovered_nodes[packet->src_id] = 1;
							    last_heard_node_id = packet->src_id;

							    // Update remote node's motion state
							    remote_node_motion_state = packet->motion_state;

							    // Print discovery information
							    unsigned long timestamp = clock_time();
							    printf("\n%lu DETECT %u", timestamp / CLOCK_SECOND, packet->src_id);

							    // If both nodes are stationary for required time, link quality is good,
							    // and we have data to send (Node A)
							    if (node_id == NODE_A_ID && 
									    motion_state == MOTION_STATE_STATIONARY && 
									    remote_node_motion_state == MOTION_STATE_STATIONARY &&
									    is_link_quality_good() && 
									    data_collection_complete && 
									    last_sent_index < current_reading_index) {

								    ready_to_transfer = 1;
								    unsigned long timestamp = clock_time();
								    printf("\n%lu TRANSFER %u RSSI: %d", 
										    timestamp / CLOCK_SECOND, packet->src_id, get_average_rssi());
							    }
						    }
						    break;
					    }

		case PACKET_TYPE_DATA_REQUEST: {
						       if (len == sizeof(data_request_packet_t) && 
								       node_id == NODE_A_ID && 
								       motion_state == MOTION_STATE_STATIONARY && 
								       remote_node_motion_state == MOTION_STATE_STATIONARY) {

							       data_request_packet_t *request = (data_request_packet_t*)data;

							       // Send data response with sensor readings
							       data_packet_t response;
							       response.type = PACKET_TYPE_DATA_RESPONSE;
							       response.src_id = node_id;

							       // Determine what data to send
							       uint16_t start_index = request->last_index;
							       uint8_t readings_to_send = (current_reading_index - start_index > 10) ? 
								       10 : (current_reading_index - start_index);

							       if (readings_to_send > 0) {
								       response.start_index = start_index;
								       response.num_readings = readings_to_send;

								       // Copy readings
								       for (uint8_t i = 0; i < readings_to_send; i++) {
									       response.light_readings[i] = light_readings[start_index + i];
									       response.motion_readings[i] = motion_readings[start_index + i];
								       }

								       // Send the response
								       nullnet_buf = (uint8_t *)&response;
								       nullnet_len = sizeof(data_packet_t);
								       NETSTACK_NETWORK.output(src);

								       // Update last sent index
								       last_sent_index = start_index + readings_to_send;
							       }
						       }
						       break;
					       }

		case PACKET_TYPE_DATA_RESPONSE: {
							if (len == sizeof(data_packet_t) && 
									node_id == NODE_B_ID && 
									motion_state == MOTION_STATE_STATIONARY) {

								data_packet_t *response = (data_packet_t*)data;

								// Print received data
								printf("\nReceived %u light and motion readings from Node %u", 
										response->num_readings, response->src_id);

								// Print light readings
								printf("\nLight: ");
								for (uint8_t i = 0; i < response->num_readings; i++) {
									printf("%u", response->light_readings[i]);
									if (i < response->num_readings - 1) printf(", ");
								}

								// Print motion readings
								printf("\nMotion: ");
								for (uint8_t i = 0; i < response->num_readings; i++) {
									printf("%u", response->motion_readings[i]);
									if (i < response->num_readings - 1) printf(", ");
								}

								// Request next batch of data if needed
								uint16_t last_index = response->start_index + response->num_readings;
								if (response->num_readings == 10) {  // There might be more data
									data_request_packet_t request;
									request.type = PACKET_TYPE_DATA_REQUEST;
									request.src_id = node_id;
									request.last_index = last_index;

									nullnet_buf = (uint8_t *)&request;
									nullnet_len = sizeof(data_request_packet_t);
									NETSTACK_NETWORK.output(src);
								}
							}
							break;
						}

		case PACKET_TYPE_MOTION_STATUS: {
							if (len == sizeof(motion_status_packet_t)) {
								motion_status_packet_t *status = (motion_status_packet_t*)data;
								remote_node_motion_state = status->motion_state;

								printf("\nNode %u motion state: %s", 
										status->src_id, 
										status->motion_state == MOTION_STATE_STATIONARY ? "STATIONARY" : "ACTIVE");
							}
							break;
						}
	}
}

// Scheduler function for neighbor discovery
char sender_scheduler(struct rtimer *t, void *ptr) {
	static uint16_t i = 0;
	static int sleep_cycle = 0;

	// Begin the protothread
	PT_BEGIN(&pt);

	// Get the current time stamp
	unsigned long curr_timestamp = clock_time();
	printf("\nStart clock %lu ticks, timestamp %3lu.%03lu", curr_timestamp, 
			curr_timestamp / CLOCK_SECOND, 
			((curr_timestamp % CLOCK_SECOND)*1000) / CLOCK_SECOND);

	while (1) {
		// Turn radio on
		NETSTACK_RADIO.on();

		// Send discovery packets
		for (i = 0; i < NUM_SEND; i++) {
			discovery_packet_t packet;
			packet.type = PACKET_TYPE_DISCOVERY;
			packet.src_id = node_id;
			packet.last_heard_id = last_heard_node_id;
			packet.seq = discovery_seq++;
			packet.motion_state = motion_state;

			nullnet_buf = (uint8_t *)&packet;
			nullnet_len = sizeof(discovery_packet_t);
			NETSTACK_NETWORK.output(&dest_addr);

			// If Node B detected Node A with good link quality, both nodes are stationary,
			// send data request
			if (node_id == NODE_B_ID && 
					discovered_nodes[NODE_A_ID] && 
					is_link_quality_good() && 
					motion_state == MOTION_STATE_STATIONARY && 
					remote_node_motion_state == MOTION_STATE_STATIONARY) {

				data_request_packet_t request;
				request.type = PACKET_TYPE_DATA_REQUEST;
				request.src_id = node_id;
				request.last_index = 0;  // Start from the beginning

				nullnet_buf = (uint8_t *)&request;
				nullnet_len = sizeof(data_request_packet_t);
				NETSTACK_NETWORK.output(&dest_addr);
			}

			// Wait between packets during wake time
			if (i < NUM_SEND - 1) {
				rtimer_set(t, RTIMER_TIME(t) + WAKE_TIME/NUM_SEND, 1, 
						(rtimer_callback_t)sender_scheduler, ptr);
				PT_YIELD(&pt);
			}
		}

		// Turn radio off to save power
		NETSTACK_RADIO.off();

		// Sleep for a random number of slots
		sleep_cycle = MIN_SLEEP_CYCLE + (random_rand() % (MAX_SLEEP_CYCLE - MIN_SLEEP_CYCLE + 1));
		printf("\nSleep for %d slots", sleep_cycle);

		for (i = 0; i < sleep_cycle; i++) {
			rtimer_set(t, RTIMER_TIME(t) + SLEEP_SLOT, 1, 
					(rtimer_callback_t)sender_scheduler, ptr);
			PT_YIELD(&pt);
		}
	}

	PT_END(&pt);
}

// Main process
PROCESS_THREAD(bonus, ev, data) {
	PROCESS_BEGIN();

	// Initialize nullnet
	nullnet_set_input_callback(receive_packet_callback);
	linkaddr_copy(&dest_addr, &linkaddr_null);  // Use broadcast address

	printf("\nMotion-Triggered Backhauling Network - Node %u", node_id);

	// Initialize variables
	reset_rssi_history();
	memset(discovered_nodes, 0, sizeof(discovered_nodes));

	// Initialize the accelerometer
	adxl362_init();

	// Start the neighbor discovery process
	rtimer_set(&rt, RTIMER_NOW() + (RTIMER_SECOND / 1000), 1, 
			(rtimer_callback_t)sender_scheduler, NULL);

	// Start timer for motion state checking
	etimer_set(&motion_check_timer, MOTION_CHECK_INTERVAL);

	// Main loop
	while (1) {
		PROCESS_WAIT_EVENT();

		// Check for motion state change periodically
		if (etimer_expired(&motion_check_timer)) {
			update_motion_state();
			etimer_reset(&motion_check_timer);

			// For Node A: If significant motion detected and not already collecting data
			if (node_id == NODE_A_ID && 
					!data_collection_active && 
					motion_state == MOTION_STATE_ACTIVE && 
					check_significant_motion()) {

				// Start sensing cycle
				printf("\nNode A: Significant motion detected, starting data collection");
				data_collection_active = 1;
				data_collection_complete = 0;
				current_reading_index = 0;
				last_sent_index = 0;

				// Initialize sensor readings array
				for (uint16_t i = 0; i < MAX_READINGS; i++) {
					light_readings[i] = 0;
					motion_readings[i] = 0;
				}

				// Set timer for first reading
				etimer_set(&sensing_timer, CLOCK_SECOND / 20);  // Start almost immediately
			}
		}

		// For Node A: If collecting data and sensing timer expired
		if (node_id == NODE_A_ID && 
				data_collection_active && 
				etimer_expired(&sensing_timer)) {

			// Read sensors
			light_readings[current_reading_index] = read_light_sensor();
			motion_readings[current_reading_index] = read_motion_sensor();

			printf("\nReading %u: Light=%u, Motion=%u", 
					current_reading_index, 
					light_readings[current_reading_index], 
					motion_readings[current_reading_index]);

			// Increment reading index
			current_reading_index++;

			// Check if collection is complete
			if (current_reading_index >= MAX_READINGS) {
				printf("\nNode A: Data collection complete (%u readings)", current_reading_index);
				data_collection_complete = 1;
				data_collection_active = 0;
			} else {
				// Set timer for next reading
				etimer_set(&sensing_timer, SENSING_INTERVAL);
			}
		}

		// Node A: If ready to transfer and good link quality with Node B
		// (and both nodes stationary)
		if (node_id == NODE_A_ID && 
				ready_to_transfer && 
				motion_state == MOTION_STATE_STATIONARY && 
				remote_node_motion_state == MOTION_STATE_STATIONARY) {

			// Wait for data request from Node B (handled in callback)
			ready_to_transfer = 0;
		}
	}

	PROCESS_END();
}
