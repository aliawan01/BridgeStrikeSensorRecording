#include "pcp.hpp"
#include "sensor_config.hpp"

#define ArrayCount(x) sizeof(x)/sizeof(x[0])

using namespace Azure::Storage::Blobs;

std::atomic<SensorConfig> global_sensor_config{};
std::mutex data_mutex;
std::binary_semaphore ready_to_try_saving_data_semaphore{0};
std::atomic<int> semaphore_count{0};
std::atomic<bool> sensor_config_changed{false};
std::string base_message_string = 
  "token=your_token&"
  "user=your_user&"
  "html=1&"
  "title=New+Recording&"
  "message=";

struct SaveData {
  std::atomic<bool> reset;
  int timer_ms;
  std::unordered_map<std::string, std::vector<std::vector<float>>>& nodes_reading_dict;
  std::vector<std::vector<uint64_t>>& nodes_timestamp_array;
};

constexpr char* parquet_data_directory_name = (char* const)"parquet-data";

void shared_memory_cleanup(int signal) {
  fmt::print("Unmapped and unlinked shared memory and semaphores.\n");

  munmap(shared_memory, SHARED_MEMORY_SIZE);
  shm_unlink(shared_memory_name);

  sem_close(client_sem);
  sem_close(server_sem);

  sem_unlink(client_sem_name);
  sem_unlink(server_sem_name);

  exit(0);
}

void thread_save_data_timer(SaveData& save_data) {
  sleep:
  fmt::print("Above semaphore acquire line\n");
  ready_to_try_saving_data_semaphore.acquire();
  semaphore_count--;
  fmt::print("INSIDE THREAD: semaphore_count decremented: {}\n", semaphore_count.load());

  fmt::print("Acquired semaphore now sleeping acquire line\n");
  std::this_thread::sleep_for(std::chrono::milliseconds(save_data.timer_ms));
  fmt::print("Finished sleeping sempahore\n");

  if (save_data.reset.load()) {
    bool expected = true;
    save_data.reset.compare_exchange_strong(expected, false);
    goto sleep;
  }

  // TODO(ali): Figure out how to send requests with pushover
  if (!std::filesystem::exists(parquet_data_directory_name)) {
    std::filesystem::create_directory(parquet_data_directory_name);
    fmt::print("Created {} directory!!\n", parquet_data_directory_name);
  }

  std::lock_guard<std::mutex> lock(data_mutex);

  SensorConfig config = global_sensor_config.load();

  // TODO(ali): Could just set this up in the main function then pass it on or just set it as a global variable
  std::vector<std::shared_ptr<arrow::Field>> schema_values = {
      arrow::field("SystemTime", arrow::int64()),
  };

  for (auto& it : save_data.nodes_reading_dict) {
    schema_values.push_back(arrow::field(it.first, arrow::float32()));
  }

  std::shared_ptr<arrow::Schema> schema = arrow::schema(schema_values);

  time_t global_time = time(NULL);
  struct tm* current_time = localtime(&global_time);

  char date_time_buffer[100] = {};
  char pushover_date_buffer[100] = {};
  char pushover_start_time_buffer[100] = {};

  strftime(date_time_buffer, sizeof(date_time_buffer), "%y%m%d-%H%M%S", current_time);
  strftime(pushover_date_buffer, sizeof(pushover_date_buffer), "%d/%m/%y", current_time);
  strftime(pushover_start_time_buffer, sizeof(pushover_start_time_buffer), "%H:%M:%S", current_time);

  for (int i = 0; i < config.node_address_count; i++) {
    if (save_data.nodes_timestamp_array[i].empty()) {
      fmt::print("{} timestamp array has no readings!\n", config.node_address_array[i]);
      continue;
    }

    int largest_array_size = save_data.nodes_timestamp_array[i].size();
    fmt::print("\tTimestamp array length {}\n", largest_array_size);
    for (auto& it : save_data.nodes_reading_dict) {
      if (it.second[i].size() > largest_array_size) {
        largest_array_size = it.second[i].size();
      }
      fmt::print("\t{}: Reading length: {}\n", it.first, it.second[i].size());
    }

    std::string rms_and_max_value_string = "";
    for (auto& it : save_data.nodes_reading_dict) {
      double total = 0;
      double max = 0;
      if (it.second[i].size() > 0) {
        for (auto& val : it.second[i]) {
          total += (std::abs(val) * std::abs(val));
        }

        max = *std::max_element(std::begin(it.second[i]), std::end(it.second[i]));
        double rms = std::sqrt((double)(total/it.second[i].size()));

        //fmt::print("Channel: {}, rms value: {}, raw total: {}, raw size: {}, max value: {}\n", it.first, rms, total, it.second[i].size(), max);
        rms_and_max_value_string.append(
            fmt::format(
              "<p>{0} RMS Value: {1}</p>\n"
              "<p>{0} Maximum Value: {2}</p>\n",
              it.first,
              rms,
              max)
        );
      }

      int difference = largest_array_size - it.second[i].size();
      if (difference > 0) {
        fmt::print("Changed channel: '{}', largest_array_size: {}, channel reading size: {}, difference: {}\n", it.first, largest_array_size, it.second.size(), difference);
        for (int z = 0; z < difference; z++) {
          it.second[i].push_back(0);
        }
      }
    }

    auto duration = (save_data.nodes_timestamp_array[i].back()-save_data.nodes_timestamp_array[i].front())/1000000000;

    arrow::Status status;

    arrow::Int64Builder system_time_builder;
    const long* nodes_timestamp_array_start = reinterpret_cast<const long*>(&(save_data.nodes_timestamp_array[i][0]));
    status = system_time_builder.AppendValues(nodes_timestamp_array_start, save_data.nodes_timestamp_array[i].size());
    std::shared_ptr<arrow::Array> system_time_array = system_time_builder.Finish().ValueOrDie();
    std::shared_ptr<arrow::ChunkedArray> system_time_chunks = std::make_shared<arrow::ChunkedArray>(system_time_array);

    std::vector<std::shared_ptr<arrow::ChunkedArray>> chunks_array = {system_time_chunks};

    for (auto& it : save_data.nodes_reading_dict) {
      arrow::FloatBuilder channel_builder;

      const float* node_readings_array_start = reinterpret_cast<const float*>(&(it.second[i][0]));
      status = channel_builder.AppendValues(node_readings_array_start, it.second[i].size());

      auto channel_array = channel_builder.Finish().ValueOrDie();
      auto channel_chunks = std::make_shared<arrow::ChunkedArray>(channel_array);
      chunks_array.push_back(channel_chunks);
    }

    std::shared_ptr<arrow::Table> table = arrow::Table::Make(schema, chunks_array);

    std::shared_ptr<parquet::WriterProperties> properties = parquet::WriterProperties::Builder().compression(arrow::Compression::SNAPPY)->build();

    std::string filename = fmt::format("nodeaddress-{}_{}_{}.parquet", config.node_address_array[i], date_time_buffer, duration);
    std::string filepath = fmt::format("{}/{}", parquet_data_directory_name, filename);
    std::shared_ptr<arrow::io::FileOutputStream> file = arrow::io::FileOutputStream::Open(filepath).ValueOrDie();
    status = parquet::arrow::WriteTable(*table.get(), arrow::default_memory_pool(), file);
    fmt::print("=====================\nSuccessfully saved parquet filename: {}, filepath: {}\n=====================\n", filename, filepath);

    const char* container_string = "your_connection_string";
    BlobServiceClient blob_service_client = BlobServiceClient::CreateFromConnectionString(container_string);
    BlobContainerClient blob_container_client = blob_service_client.GetBlobContainerClient("sensorstrikedata");

    BlockBlobClient blob_client = blob_container_client.GetBlockBlobClient(filename);
    blob_client.UploadFrom(filepath);

    fmt::print("=====================\nSuccessfully sent '{}' to the Azure Blob Container\n=====================\n", filename);

    std::string pushover_html_string = fmt::format(
        "<h1>Filename: {}</h1>\n\n"
        "<p>Duration: {}s</p>\n"
        "<p>Date: {}</p>\n"
        "<p>Start Time: {}</p>\n",
        filename,
        duration,
        pushover_date_buffer,
        pushover_start_time_buffer
    );

    pushover_html_string.append(rms_and_max_value_string);
    pushover_html_string.append(
        fmt::format("\n<a href='http://localhost:1234/?filename={}'>Link to Visualize Data</a>\n", filename)
    );

    fmt::print("Pushover html string:\n{}\n", pushover_html_string);

    CURL* curl = curl_easy_init();

    char* encoded_pushover_string = curl_easy_escape(curl, pushover_html_string.c_str(), pushover_html_string.length());

    std::string message_string = base_message_string;
    message_string.append(encoded_pushover_string);

    curl_free(encoded_pushover_string);

    struct curl_slist* headers_list = NULL;

    std::string content_length_string = fmt::format("Content-Length: {}", message_string.length());

    headers_list = curl_slist_append(headers_list, "Host: api.pushover.net");
    headers_list = curl_slist_append(headers_list, "Content-Type: application/x-www-form-urlencoded");
    headers_list = curl_slist_append(headers_list, content_length_string.c_str());

    curl_easy_setopt(curl, CURLOPT_URL, "https://api.pushover.net/1/messages.json");
    curl_easy_setopt(curl, CURLOPT_POST, 1);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers_list);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, message_string.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, message_string.length());

    CURLcode output = curl_easy_perform(curl);
    if (output) {
      fmt::print(stderr, "[CURL ERROR] curl_easy_perform: {}", curl_easy_strerror(output));
    }
    else {
      fmt::print("\n[CURL] Successfully sent Pushover notification!\n");
    }

    curl_slist_free_all(headers_list);

    curl_easy_cleanup(curl);
  }

  fmt::print("Clearing out arrays\n");
  for (auto& timestamp_array : save_data.nodes_timestamp_array) {
    timestamp_array.clear();
  }

  for (auto& reading : save_data.nodes_reading_dict) {
    for (auto& reading_array : reading.second) {
      reading_array.clear();
    }
  }

  goto sleep;
}

void sensor_config_update_thread(void) {
  while (true) {
    sem_wait(server_sem);

    SensorConfig sensor_config = {};
    sensor_config.read_from_shared_memory(shared_memory);
    global_sensor_config.store(sensor_config);

    bool expected = false;
    sensor_config_changed.compare_exchange_strong(expected, true);
    fmt::print("[SENSOR CONFIG RELOAD] Set sensor_config_changed: true in sensor config update thread\n");
  }
}

int main(void) {
  signal(SIGINT, shared_memory_cleanup);
  bool setup_initial_sensor_config = false;

  SensorConfig sensor_config = {};
  sensor_config.node_address_count = 2;
  sensor_config.node_address_array = (uint32_t*)malloc(sizeof(*sensor_config.node_address_array)*sensor_config.node_address_count);
  sensor_config.node_address_array[0] = 29120;
  sensor_config.node_address_array[1] = 40154;

  server_sem = sem_open(server_sem_name, O_CREAT, 0666, 0);
  if (server_sem == SEM_FAILED) {
    fmt::print("[SERVER] Error occured when creating the server semaphore:\n{}\n", strerror(errno));
    return -1;
  }
  else {
    fmt::print("[SERVER] Successfully created the server semaphore!\n");
  }

  client_sem = sem_open(client_sem_name, O_CREAT, 0666, 0);
  if (client_sem == SEM_FAILED) {
    fmt::print("[SERVER] Error occured when creating the client semaphore:\n{}\n", strerror(errno));
    return -1;
  }
  else {
    fmt::print("[SERVER] Successfully created the client semaphore!\n");
  }

  int shared_memory_fd = shm_open(shared_memory_name, O_CREAT|O_RDWR, 0666);
  if (shared_memory_fd == -1) {
    fmt::print("[SERVER] Error occured when creating the shared memory:\n{}\n", strerror(errno));
    return -1;
  }
  else {
    fmt::print("[SERVER] Successfully created shared memory! fd: {}\n", shared_memory_fd);
  }

  ftruncate(shared_memory_fd, SHARED_MEMORY_SIZE);
  shared_memory = (uint8_t*)mmap(NULL, SHARED_MEMORY_SIZE, PROT_READ|PROT_WRITE, MAP_SHARED, shared_memory_fd, 0);

  int result = curl_global_init(CURL_GLOBAL_ALL);
  if (result) {
    fmt::print("[CURL] Failed to initialize libcurl: {}\n", result);
  }
  else {
    fmt::print("[CURL] Successfully initialized libcurl\n", result);
  }

  std::vector<mscl::WirelessNode> wireless_nodes_array;
  wireless_nodes_array.reserve(sensor_config.node_address_count);

  std::vector<std::vector<uint64_t>> nodes_timestamp_array;
  std::unordered_map<std::string, std::vector<std::vector<float>>> nodes_reading_dict = {};

  SaveData save_data = {
    std::atomic<bool>{false},
    2000,
    nodes_reading_dict,
    nodes_timestamp_array
  };

  std::thread save_data_thread(thread_save_data_timer, std::ref(save_data));
  std::thread sensor_update_thread(sensor_config_update_thread);

  try {
    mscl::Connection connection = mscl::Connection::Serial("/dev/ttyUSB0");

    int num_of_tries_to_connect_to_base_station = 0;
    constexpr int max_tries_to_connect_to_base_station = 20;

    mscl::BaseStation* base_station_ptr;
    try {
      base_station_ptr = new mscl::BaseStation(connection);
      fmt::print("Firmware: {}\n", base_station_ptr->firmwareVersion().str());
    }
    catch (mscl::Error& error) {
      fmt::print("[BASE STATION] Error when connecting to base station: {}\n", error.what());
      fmt::print("Failed to connect to Base Station times, could be one of the following issues:\n");
      fmt::print("\t- Make sure the cable is plugged in if it is, remove it and plug it in again.\n");
      fmt::print("\t- Version of libmscl.a could be incorrect/outdated.\n");
      fmt::print("\t- There is already another instance of the program running.\n");
      return -1;
    }

    mscl::BaseStation base_station = *base_station_ptr;

    //base_station.enableBeacon();
    setup_nodes:
    if (setup_initial_sensor_config) {
      fmt::print("[SENSOR CONFIG RELOAD] Reloading in the main loop\n");

      sensor_config = global_sensor_config.load();

      fmt::print("node_address_count: {}\n", sensor_config.node_address_count);
      for (int i = 0; i < sensor_config.node_address_count; i++) {
        fmt::print("\tnode_address_array[{}]: {}\n", i, sensor_config.node_address_array[i]);
      } 
      fmt::print("num_of_channels_available: {}\n", sensor_config.num_of_channels_available);
      for (int i = 0; i < sensor_config.num_of_channels_available; i++) {
        fmt::print("\tchannel_description_array[{}]: {}\n", i, sensor_config.channel_description_array[i]);
      } 
      fmt::print("enabled_channels_array: {}\n", std::span<uint8_t>(sensor_config.enabled_channels_array, sensor_config.enabled_channels_count));
      fmt::print("trigger_enabled_array: {}\n", std::span<uint8_t>(sensor_config.trigger_enabled_array, sensor_config.enabled_channels_count));
      fmt::print("trigger_pre_post_duration_array: {}\n", std::span<uint32_t>(sensor_config.trigger_pre_post_duration_array, sensor_config.node_address_count*2));
      fmt::print("trigger_value_array: {}\n", std::span<float>(sensor_config.trigger_value_array, sensor_config.enabled_channels_count));


      wireless_nodes_array.clear();
      nodes_reading_dict.clear();
      nodes_timestamp_array.clear();
    }

    for (int i = 0; i < sensor_config.node_address_count; i++) {
      int node_address = sensor_config.node_address_array[i];
      wireless_nodes_array.push_back((mscl::WirelessNode) {
        static_cast<mscl::NodeAddress>(node_address),
        base_station
      });
    }

    for (int i = 0; i < sensor_config.node_address_count; i++) {
      std::vector<uint64_t> timestamp_array;
      timestamp_array.reserve(3000);

      nodes_timestamp_array.push_back(timestamp_array);
    }

    mscl::SyncSamplingNetwork network(base_station);

    std::vector<int> invalid_node_address_array = {};
    for (int i = 0; i < wireless_nodes_array.size(); i++) {
      auto wireless_node = wireless_nodes_array[i];

      wireless_node.readWriteRetries(10);

      set_to_idle:
      try {
        mscl::SetToIdleStatus idle_status_successful = wireless_node.setToIdle();

        bool is_invalid_node = false;
        auto starting_time = std::chrono::system_clock::now();
        while(!idle_status_successful.complete())
        {
          auto current_time = std::chrono::system_clock::now();
          if (std::chrono::duration_cast<std::chrono::milliseconds>(current_time-starting_time) > std::chrono::milliseconds(20000)) {
            fmt::print("\nFound invalid node: {}\n", wireless_node.nodeAddress());
            is_invalid_node = true;
            break;
          }

          fmt::print(".");
        }

        if (is_invalid_node) {
          invalid_node_address_array.push_back(wireless_node.nodeAddress());
          wireless_nodes_array.erase(wireless_nodes_array.begin()+i);
          i--;
          continue;
        }

        switch(idle_status_successful.result())
        {
          case mscl::SetToIdleStatus::setToIdleResult_success:
            fmt::print("\n[Node: {}] Now in idle mode\n", wireless_node.nodeAddress());
          break;

          case mscl::SetToIdleStatus::setToIdleResult_canceled:
            fmt::print("\n[Node: {}] Set to Idle was canceled!\n", wireless_node.nodeAddress());
          break;

          case mscl::SetToIdleStatus::setToIdleResult_failed:
            fmt::print("\n[Node: {}] Set to Idle has failed!\n", wireless_node.nodeAddress());
          break;

          case mscl::SetToIdleStatus::setToIdleResult_notCompleted:
            fmt::print("\n[Node: {}] Set to Idle didn't complete!\n", wireless_node.nodeAddress());
          break;
        }
      } 
      catch (mscl::Error& error) {
        fmt::print("setToIdle Error {}: {}\n", wireless_node.nodeAddress(), error.what());
        goto set_to_idle;
      }

      wireless_node.useEepromCache(true);

      const mscl::NodeFeatures* wireless_node_features_ptr; 
      while (true) {
        try {
          wireless_node_features_ptr = &wireless_node.features();
          break;
        }
        catch (mscl::Error& error) {
          fmt::print("wireless_node.features() Error: {}\n", error.what());
          fmt::print("Trying to retreieve the WirelessNode features again...\n");
        }
      }

      const mscl::NodeFeatures& wireless_node_features = *wireless_node_features_ptr;

      mscl::WirelessNodeConfig config;

      config.defaultMode(mscl::WirelessTypes::defaultMode_idle);
      config.inactivityTimeout(7200);
      config.samplingMode(mscl::WirelessTypes::samplingMode_syncEvent);
      config.sampleRate(mscl::WirelessTypes::sampleRate_256Hz);
      config.unlimitedDuration(true);

      auto channel_mask = mscl::ChannelMask();

      if (!setup_initial_sensor_config) {
        channel_mask.enable(3);
        channel_mask.enable(5);
        channel_mask.enable(2);
        channel_mask.enable(1);
      }
      else {
        for (int channel_base_index = 0; channel_base_index < sensor_config.num_of_channels_available; channel_base_index++) {
          if (sensor_config.enabled_channels_array[(sensor_config.num_of_channels_available*i)+channel_base_index]) {
            channel_mask.enable(channel_base_index+1);
          }
        }
        fmt::print("[SENSOR CONFIG RELOAD] Setup channels\n");
      }

      for (int i = 1; i < 9; i++) {
        fmt::print("Channel {} enabled: {}\n", i, channel_mask.enabled(i));
        if (channel_mask.enabled(i)) {
          std::vector<std::vector<float>> value;
          for (int z = 0; z < sensor_config.node_address_count; z++) {
            value.push_back({});
          }

          nodes_reading_dict[fmt::format("ch{}", i)] = value;
        }
      }

      config.activeChannels(channel_mask);

      auto event_trigger_options = wireless_node.getEventTriggerOptions();

      if (!setup_initial_sensor_config) {
        fmt::print("Trigger options size {}: {}\n", wireless_node.nodeAddress(), event_trigger_options.triggers().size());
        event_trigger_options.preDuration(2000);
        event_trigger_options.postDuration(3000);

        event_trigger_options.enableTrigger(0, true);
        auto trigger = event_trigger_options.trigger(0);

        trigger.channelNumber(3);
        trigger.triggerType(mscl::WirelessTypes::eventTrigger_ceiling);

        // NOTE(ali): Trigger threshold
        for (int i = 1; i < event_trigger_options.triggers().size(); i++) {
          event_trigger_options.enableTrigger(i, false);
        }
      }
      else {
        bool any_channels_enabled = false;
        bool any_triggers_enabled = false;

        for (int channel_base_index = 0; channel_base_index < sensor_config.num_of_channels_available; channel_base_index++) {
          if (sensor_config.enabled_channels_array[(sensor_config.num_of_channels_available*i)+channel_base_index]) {
            any_channels_enabled = true;
          }

          if (sensor_config.trigger_enabled_array[(sensor_config.num_of_channels_available*i)+channel_base_index]) {
            any_triggers_enabled = true;
          }
        }

        if (any_channels_enabled && any_triggers_enabled) {
          fmt::print("pre duration unnormalized: {}\n", sensor_config.trigger_pre_post_duration_array[i*2]);
          fmt::print("post duration unnormalized: {}\n\n", sensor_config.trigger_pre_post_duration_array[(i*2)+1]);

          fmt::print("pre duration normalized: {}\n", wireless_node_features.normalizeEventDuration(sensor_config.trigger_pre_post_duration_array[i*2]));
          fmt::print("post duration normalized: {}\n", wireless_node_features.normalizeEventDuration(sensor_config.trigger_pre_post_duration_array[(i*2)+1]));

          event_trigger_options.preDuration(wireless_node_features.normalizeEventDuration(sensor_config.trigger_pre_post_duration_array[i*2]));
          event_trigger_options.preDuration(wireless_node_features.normalizeEventDuration(sensor_config.trigger_pre_post_duration_array[(i*2)+1]));

          for (int channel_base_index = 0; channel_base_index < sensor_config.num_of_channels_available; channel_base_index++) {
            if (sensor_config.trigger_enabled_array[(sensor_config.num_of_channels_available*i)+channel_base_index]) {
              event_trigger_options.enableTrigger(channel_base_index, true);
              auto trigger = event_trigger_options.trigger(channel_base_index);

              trigger.channelNumber(channel_base_index+1);
              trigger.triggerType(mscl::WirelessTypes::eventTrigger_ceiling);
              trigger.triggerValue(sensor_config.trigger_value_array[(sensor_config.num_of_channels_available*i)+channel_base_index]);
            }
            else {
              event_trigger_options.enableTrigger(channel_base_index, false);
            }
          }

          fmt::print("[SENSOR CONFIG RELOAD] Setup triggers\n");
        }
      }

      config.eventTriggerOptions(event_trigger_options);

      wireless_node.applyConfig(config);

      network.addNode(wireless_node);
    }

    if (!setup_initial_sensor_config) {
      setup_initial_sensor_config = true;
      sensor_config.num_of_channels_available = 100;

      std::vector<std::string> channel_description_array;
      std::vector<uint8_t>     trigger_enabled_array;
      std::vector<uint32_t>    trigger_pre_post_duration_array;
      std::vector<float>       trigger_value_array;

      for (int wireless_node_index = 0; wireless_node_index < wireless_nodes_array.size(); wireless_node_index++) {
        auto& wireless_node = wireless_nodes_array[wireless_node_index];

        const mscl::NodeFeatures* wireless_node_features_ptr; 
        while (true) {
          try {
            wireless_node_features_ptr = &wireless_node.features();
            break;
          }
          catch (mscl::Error& error) {
            fmt::print("wireless_node.features() Error: {}\n", error.what());
            fmt::print("Trying to retreieve the WirelessNode features again...\n");
          }
        }

        const mscl::NodeFeatures& wireless_node_features = *wireless_node_features_ptr;

        int channels_size = wireless_node_features.channels().size();
        sensor_config.num_of_channels_available = 
          channels_size < sensor_config.num_of_channels_available ? channels_size : sensor_config.num_of_channels_available;

        for (auto& feature : wireless_node_features.channels()) {
          bool description_exists = false;
          for (auto& description : channel_description_array) {
            if (description == feature.description()) {
              description_exists = true;
              break;
            }
          }

          if (!description_exists) {
            channel_description_array.push_back(feature.description());
          }
        }

        auto trigger_options = wireless_node.getEventTriggerOptions();

        // TODO(ali): This will fail miserably and write incorrect data if all the nodes
        //            don't have the same channels.
        for (int i = 0; i < channel_description_array.size(); i++) {
          trigger_enabled_array.push_back(0);
          trigger_value_array.push_back(0.f);
        }

        if (trigger_options.anyTriggersEnabled()) {
          trigger_pre_post_duration_array.push_back(trigger_options.preDuration());
          trigger_pre_post_duration_array.push_back(trigger_options.postDuration());

          for (int i = 0; i < trigger_options.triggers().size(); i++) {
            if (trigger_options.triggerEnabled(i)) {
              auto trigger = trigger_options.trigger(i);
              int channel_number = trigger.channelNumber();
              trigger_enabled_array[(wireless_node_index*channel_description_array.size())+channel_number-1] = 1;
              trigger_value_array[(wireless_node_index*channel_description_array.size())+channel_number-1] = trigger.triggerValue();
            }
          }
        }
        else {
          trigger_pre_post_duration_array.push_back(0);
          trigger_pre_post_duration_array.push_back(0);
        }
      }

      std::vector<char*> channel_description_c_string_array;
      for (auto& description : channel_description_array) {
        channel_description_c_string_array.push_back(const_cast<char*>(description.c_str()));
      }

      sensor_config.channel_description_array = channel_description_c_string_array.data();

      sensor_config.enabled_channels_count = sensor_config.node_address_count*sensor_config.num_of_channels_available;
      sensor_config.enabled_channels_array = (uint8_t*)malloc(sensor_config.enabled_channels_count);
      for (int z = 0; z < wireless_nodes_array.size(); z++) {
        auto channel_mask = wireless_nodes_array[z].getActiveChannels();

        for (int i = 1; i < sensor_config.num_of_channels_available+1; i++) {
          fmt::print("index being used: {}, max size: {}\n", (z*sensor_config.num_of_channels_available)+i-1, sensor_config.num_of_channels_available);
          sensor_config.enabled_channels_array[(z*sensor_config.num_of_channels_available)+i-1] = channel_mask.enabled(i);
        }
      }

      fmt::print("node_address_count: {}\n", sensor_config.node_address_count);
      for (int i = 0; i < sensor_config.node_address_count; i++) {
        fmt::print("\tnode_address_array[{}]: {}\n", i, sensor_config.node_address_array[i]);
      } 
      fmt::print("num_of_channels_available: {}\n", sensor_config.num_of_channels_available);
      for (int i = 0; i < sensor_config.num_of_channels_available; i++) {
        fmt::print("\tchannel_description_array[{}]: {}\n", i, sensor_config.channel_description_array[i]);
      } 
      fmt::print("enabled_channels_array: {}\n", std::span<uint8_t>(sensor_config.enabled_channels_array, sensor_config.enabled_channels_count));
      fmt::print("trigger_enabled_array: {}\n", trigger_enabled_array);
      fmt::print("trigger_pre_post_duration_array: {}\n", trigger_pre_post_duration_array);
      fmt::print("trigger_value_array: {}\n", trigger_value_array);

      sensor_config.trigger_enabled_array = trigger_enabled_array.data();
      sensor_config.trigger_pre_post_duration_array = trigger_pre_post_duration_array.data();
      sensor_config.trigger_value_array = trigger_value_array.data();
      sensor_config.error_code = 0;

      sensor_config.write_to_shared_memory(shared_memory);
    }

    if (!invalid_node_address_array.empty()) {
      sensor_config.error_code = 1;
      // TODO(ali): Construct the error string

      std::string error_string = "";
      for (int invalid_address : invalid_node_address_array) {
        fmt::print("[SENSOR CONFIG RELOAD] Found invalid address: {}\n", invalid_address);

        error_string.append(fmt::format("Invalid node address: {}\n", invalid_address));

        int invalid_node_index = -1;

        for (int i = 0; i < sensor_config.node_address_count; i++) {
          if (sensor_config.node_address_array[i] == invalid_address) {
            invalid_node_index = i;
            break;
          }
        }

        if (invalid_node_index != sensor_config.node_address_count-1) {
          memmove(
              sensor_config.node_address_array+invalid_node_index, 
              sensor_config.node_address_array+invalid_node_index+1,
              sizeof(*sensor_config.node_address_array)*(sensor_config.node_address_count-invalid_node_index-1)
          );
          
          memmove(
              sensor_config.enabled_channels_array+(sensor_config.num_of_channels_available*invalid_node_index),
              sensor_config.enabled_channels_array+(sensor_config.num_of_channels_available*(invalid_node_index+1)),
              sizeof(*sensor_config.enabled_channels_array)*(sensor_config.enabled_channels_count-((invalid_node_index+1)*sensor_config.num_of_channels_available))
          );

          memmove(
              sensor_config.trigger_enabled_array+(sensor_config.num_of_channels_available*invalid_node_index),
              sensor_config.trigger_enabled_array+(sensor_config.num_of_channels_available*(invalid_node_index+1)),
              sizeof(*sensor_config.trigger_enabled_array)*(sensor_config.enabled_channels_count-((invalid_node_index+1)*sensor_config.num_of_channels_available))
          );

          memmove(
              sensor_config.trigger_pre_post_duration_array+(invalid_node_index*2),
              sensor_config.trigger_pre_post_duration_array+(invalid_node_index*3),
              sizeof(*sensor_config.trigger_pre_post_duration_array)*((sensor_config.node_address_count*2)-((invalid_node_index+1)*2))
          );

          memmove(
              sensor_config.trigger_value_array+(sensor_config.num_of_channels_available*invalid_node_index),
              sensor_config.trigger_value_array+(sensor_config.num_of_channels_available*(invalid_node_index+1)),
              sizeof(*sensor_config.trigger_value_array)*(sensor_config.enabled_channels_count-((invalid_node_index+1)*sensor_config.num_of_channels_available))
          );
        }

        sensor_config.node_address_count -= 1;
        sensor_config.enabled_channels_count -= sensor_config.num_of_channels_available;
      }

      sensor_config.error_string = const_cast<char*>(error_string.c_str());
      sensor_config.write_to_shared_memory(shared_memory);
    }

    global_sensor_config.store(sensor_config);
    sem_post(client_sem);

    fmt::print("nodes_reading_dict: {}\n", nodes_reading_dict);

    network.ok();
    fmt::print("[NETWORK] ok: SUCCESS\n");

    network.lossless(true);
    fmt::print("[NETWORK] lossless: SUCCESS\n");

    while (true) {
      try {
        network.applyConfiguration();
        break;
      }
      catch (mscl::Error_NodeCommunication& error) {
        fmt::print("[NETWORK] applyConfiguration Error_NodeCommunication: {}\n", error.what());
        fmt::print("[NETWORK] applyConfiguration Trying again...\n");
      }
      catch (mscl::Error_Connection& error) {
        fmt::print("[NETWORK] applyConfiguration Error_Connection: {}\n", error.what());
        fmt::print("[NETWORK] applyConfiguration Trying again...\n");
      }
    }

    fmt::print("[NETWORK] applyConfiguration: SUCCESS\n");

    network.startSampling();

    fmt::print("[NETWORK] startSampling: SUCCESS\n");

    int client_sem_value;
    while (true) {
      mscl::DataSweeps sweeps_array = base_station.getData(500);

      bool expected = true;
      if (sensor_config_changed.compare_exchange_weak(expected, false)) {
        expected = false;
        save_data.reset.compare_exchange_strong(expected, true);
        fmt::print("breaking out of data sweep while loop\n");
        break;
      }

      sem_getvalue(client_sem, &client_sem_value);
      if (client_sem_value <= 0) {
        sem_post(client_sem);
      }

      for (mscl::DataSweep sweep : sweeps_array) {
        auto node_address = sweep.nodeAddress();
        int node_index;

        bool is_node_valid = false;
        for (int i = 0; i < sensor_config.node_address_count; i++) {
          auto valid_node_address = sensor_config.node_address_array[i];

          if (node_address  == valid_node_address) {
            is_node_valid = true;
            node_index = i;
            break;
          }
        }

        if (!is_node_valid) {
          continue;
        }

        auto timestamp = sweep.timestamp().nanoseconds();

        for (mscl::WirelessDataPoint data : sweep.data()) {
          if (data.channelNumber() != 0) {
            const char* channel_name = data.channelName().c_str();
            float value = data.as_float();

            std::lock_guard<std::mutex> lock(data_mutex);

            if (nodes_timestamp_array[node_index].empty()) {
              nodes_timestamp_array[node_index].push_back(timestamp);
            }
            else if (nodes_timestamp_array[node_index].back() != timestamp) {
              nodes_timestamp_array[node_index].push_back(timestamp);
            }

            nodes_reading_dict[data.channelName()][node_index].push_back(value);

            if (semaphore_count.load() <= 0) {
              ready_to_try_saving_data_semaphore.release();
              semaphore_count++;
              fmt::print("MAIN THREAD: semaphore_count incremented: {}\n", semaphore_count.load());
            }

            bool expected = false;
            save_data.reset.compare_exchange_strong(expected, true);

            fmt::print("Node Address: {}, Channel Name: {}, Timestamp: {}, value: {}\n", sweep.nodeAddress(), channel_name, timestamp, value);
          }
        }
      }
    }

    fmt::print("Outside of while loop running goto setup_nodes\n");
    goto setup_nodes;
  }
  catch (mscl::Error& error) {
    fmt::print(stderr, "Error occured: {}\n", error.what());
  }

  return 0;
}
