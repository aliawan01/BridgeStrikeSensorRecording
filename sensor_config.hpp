#pragma once

#define SHARED_MEMORY_SIZE 5096

#pragma pack(push, 1)

struct SensorConfig {
  uint32_t  node_address_count;
  uint32_t* node_address_array;

  uint32_t  num_of_channels_available;
  char**    channel_description_array;

  uint32_t  enabled_channels_count;
  uint8_t*  enabled_channels_array;

  // NOTE(ali): Size: enabled_channels_count
  uint8_t*  trigger_enabled_array;

  // NOTE(ali): Size: node_address_count*2
  uint32_t* trigger_pre_post_duration_array; 

  // NOTE(ali): Size: enabled_channels_count
  float*    trigger_value_array;
  uint32_t  error_code;
  char*     error_string;

  void write_to_shared_memory(uint8_t* shared_memory);
  void read_from_shared_memory(uint8_t* shared_memory);
};

void SensorConfig::write_to_shared_memory(uint8_t* shared_memory) {
    int offset = 0;
    memcpy(shared_memory+offset, &this->node_address_count, sizeof(this->node_address_count));

    offset += sizeof(this->node_address_count);
    memcpy(shared_memory+offset, this->node_address_array, sizeof(*this->node_address_array)*this->node_address_count);

    offset += sizeof(*this->node_address_array)*this->node_address_count;
    memcpy(shared_memory+offset, &this->num_of_channels_available, sizeof(this->num_of_channels_available));

    offset += sizeof(this->num_of_channels_available);

    for (int i = 0; i < this->num_of_channels_available; i++) {
      char* buffer = *(this->channel_description_array+i);
      memcpy(shared_memory+offset, buffer, strlen(buffer)+1);
      offset += strlen(buffer)+1;
    }

    memcpy(shared_memory+offset, &this->enabled_channels_count, sizeof(this->enabled_channels_count));

    offset += sizeof(this->enabled_channels_count);
    memcpy(shared_memory+offset, this->enabled_channels_array, sizeof(*this->enabled_channels_array)*this->enabled_channels_count);

    offset += sizeof(*this->enabled_channels_array)*this->enabled_channels_count;
    memcpy(shared_memory+offset, this->trigger_enabled_array, sizeof(*this->trigger_enabled_array)*this->enabled_channels_count);

    offset += sizeof(*this->trigger_enabled_array)*this->enabled_channels_count;
    memcpy(shared_memory+offset, this->trigger_pre_post_duration_array, sizeof(*this->trigger_pre_post_duration_array)*this->node_address_count*2);

    offset += sizeof(*this->trigger_pre_post_duration_array)*this->node_address_count*2;
    memcpy(shared_memory+offset, this->trigger_value_array, sizeof(*this->trigger_value_array)*this->enabled_channels_count);

    offset += sizeof(*this->trigger_value_array)*this->enabled_channels_count;
    memcpy(shared_memory+offset, &this->error_code, sizeof(this->error_code));

    if (this->error_code > 0) {
      offset += sizeof(this->error_code);
      memcpy(shared_memory+offset, this->error_string, strlen(this->error_string)+1);
    }
}


void SensorConfig::read_from_shared_memory(uint8_t* shared_memory) {
  int data_offset = 0;
  int num_of_bytes_to_copy = 0;

  memcpy(&this->node_address_count, shared_memory+data_offset, sizeof(this->node_address_count));

  data_offset += sizeof(this->node_address_count);
  num_of_bytes_to_copy = sizeof(*this->node_address_array)*this->node_address_count;
  this->node_address_array = (uint32_t*)malloc(num_of_bytes_to_copy);
  memcpy(this->node_address_array, shared_memory+data_offset, num_of_bytes_to_copy);

  data_offset += num_of_bytes_to_copy;
  memcpy(&this->num_of_channels_available, shared_memory+data_offset, sizeof(this->num_of_channels_available));

  data_offset += sizeof(this->num_of_channels_available);

  this->channel_description_array = (char**)malloc(sizeof(*this->channel_description_array)*this->num_of_channels_available);

  for (int i = 0; i < this->num_of_channels_available; i++) {
    const char* buffer = (const char*)(shared_memory+data_offset);
    this->channel_description_array[i] = strdup(buffer);

    int size = strlen(buffer)+1;
    data_offset += size;
  }

  memcpy(&this->enabled_channels_count, shared_memory+data_offset, sizeof(this->enabled_channels_count));

  data_offset += sizeof(this->enabled_channels_count);
  num_of_bytes_to_copy = sizeof(*this->enabled_channels_array)*this->enabled_channels_count; 
  this->enabled_channels_array = (uint8_t*)malloc(num_of_bytes_to_copy);
  memcpy(this->enabled_channels_array, shared_memory+data_offset, num_of_bytes_to_copy);

  data_offset += num_of_bytes_to_copy;
  num_of_bytes_to_copy = sizeof(*this->trigger_enabled_array)*this->enabled_channels_count;
  fmt::print("trigger_enabled_array num_of_bytes_to_copy: {}\n", num_of_bytes_to_copy);
  this->trigger_enabled_array = (uint8_t*)malloc(num_of_bytes_to_copy);
  memcpy(this->trigger_enabled_array, shared_memory+data_offset, num_of_bytes_to_copy);

  data_offset += num_of_bytes_to_copy;
  num_of_bytes_to_copy = sizeof(*this->trigger_pre_post_duration_array)*this->node_address_count*2;
  this->trigger_pre_post_duration_array = (uint32_t*)malloc(num_of_bytes_to_copy);
  memcpy(this->trigger_pre_post_duration_array, shared_memory+data_offset, num_of_bytes_to_copy);

  data_offset += num_of_bytes_to_copy;
  num_of_bytes_to_copy = sizeof(*this->trigger_value_array)*this->enabled_channels_count;
  this->trigger_value_array = (float*)malloc(num_of_bytes_to_copy);
  memcpy(this->trigger_value_array, shared_memory+data_offset, num_of_bytes_to_copy);

  data_offset += num_of_bytes_to_copy;
  num_of_bytes_to_copy = sizeof(this->error_code);
  memcpy(&this->error_code, shared_memory+data_offset, num_of_bytes_to_copy);

  data_offset += num_of_bytes_to_copy;
  this->error_string = strdup((const char*)(shared_memory+data_offset));
}

#pragma pack(pop)

constexpr char* shared_memory_name = (char* const)"/sensor_config";
constexpr char* client_sem_name = (char* const)"/sensor_config_sem_client";
constexpr char* server_sem_name = (char* const)"/sensor_config_sem_server";

uint8_t* shared_memory;
sem_t* server_sem;
sem_t* client_sem;

