#include <fmt/base.h>
#include <fmt/core.h>
#include <fmt/chrono.h>
#include <fmt/ranges.h>

extern "C" {
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <semaphore.h>
#include <fcntl.h>
}

#include "sensor_config.hpp"

#define IMSPINNER_DEMO
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <imspinner.h>
#include <GLFW/glfw3.h>
#include <vector>

#define ArrayCount(x) sizeof(x)/sizeof(x[0])

void error_callback(int error, const char* description) {
    fprintf(stderr, "Error: %s\n", description);
}

void clear_client_sem(void) {
  int sem_value;
  sem_getvalue(client_sem, &sem_value);

  while (sem_value > 0) {
    sem_trywait(client_sem);
    sem_getvalue(client_sem, &sem_value);
  }
}

int main(void) {
  server_sem = sem_open(server_sem_name, 0);
  client_sem = sem_open(client_sem_name, 0);

  int shared_memory_fd = shm_open(shared_memory_name, O_RDWR, 0666);
  if (shared_memory_fd == -1) {
    fmt::print("[CLIENT] Error occured when connecting to the shared memory:\n{}\n", strerror(errno));
  }
  else {
    fmt::print("[CLIENT] Client Success! fd: {}\n", shared_memory_fd);
  }

  fmt::print("[CLIENT] Waiting above semaphore\n");

  sem_wait(client_sem);
  fmt::print("[CLIENT] Got through semaphore!\n");

  int sem_client_value;
  sem_getvalue(client_sem, &sem_client_value);
  fmt::print("sem value: {}\n", sem_client_value);

  clear_client_sem();

  shared_memory = (uint8_t*)mmap(NULL, SHARED_MEMORY_SIZE, PROT_READ|PROT_WRITE, MAP_SHARED, shared_memory_fd, 0);

  SensorConfig orig_config = {};

  orig_config.read_from_shared_memory(shared_memory);

  std::vector<uint32_t> node_address_array(orig_config.node_address_array, orig_config.node_address_array+orig_config.node_address_count);
  std::vector<std::string> channel_description_array;
  for (int i = 0; i < orig_config.num_of_channels_available; i++) {
    channel_description_array.push_back(orig_config.channel_description_array[i]);
  }

  std::vector<uint8_t> enabled_channels_array(orig_config.enabled_channels_array, orig_config.enabled_channels_array+orig_config.enabled_channels_count);
  std::vector<uint8_t> trigger_enabled_array(orig_config.trigger_enabled_array, orig_config.trigger_enabled_array+orig_config.enabled_channels_count);
  std::vector<uint32_t> trigger_pre_post_duration_array(orig_config.trigger_pre_post_duration_array, orig_config.trigger_pre_post_duration_array+(orig_config.node_address_count*2));
  std::vector<float> trigger_value_array(orig_config.trigger_value_array, orig_config.trigger_value_array+orig_config.enabled_channels_count);

  fmt::print("node_address_array: {}\n", node_address_array);
  fmt::print("channel_description_array: {}\n", channel_description_array);
  fmt::print("enabled_channels_array: {}\n", enabled_channels_array);
  fmt::print("trigger_enabled_array: {}\n", trigger_enabled_array);
  fmt::print("trigger_pre_post_duration_array: {}\n", trigger_pre_post_duration_array);
  fmt::print("trigger_value_array: {}\n", trigger_value_array);

  glfwInit();

  glfwSetErrorCallback(error_callback);

  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
  GLFWwindow* window =  glfwCreateWindow(1280, 720, "Sensor Configuration", NULL, NULL);

  glfwMakeContextCurrent(window);
  glfwSwapInterval(1);

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  ImFont* font_roboto = io.Fonts->AddFontFromFileTTF("fonts/Roboto-VariableFont_wdth,wght.ttf");

  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad; 

  ImGui_ImplGlfw_InitForOpenGL(window, true);
  ImGui_ImplOpenGL3_Init();

  ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

  bool window_open = true;
  bool demo_window = true;

  int node_address_error = 0;
  int invalid_address_length = 0;
  char already_existing_node_address[100] = {};
  char buffer[100] = {};

  int node_table_num_of_columns = 7;
  std::vector<int> opened_node_address_tabs = {};
  for (int i = 0; i < node_address_array.size(); i++) {
    opened_node_address_tabs.push_back(1);
  }

  std::string invalid_config_string = "";
  int input_text_width = 0;

  bool applying_configuration = false;

  float velocity = 1.f;
  float widget_size = 60.f;
  float nextdot = 0;

  bool saw_error_message = false;

  static ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings;

  while (!glfwWindowShouldClose(window)) {
    glfwPollEvents();

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    if (applying_configuration) {
      if (!sem_trywait(client_sem)) {
        applying_configuration = false;
        saw_error_message = false;

        free(orig_config.node_address_array);
        free(orig_config.enabled_channels_array);
        free(orig_config.trigger_enabled_array);
        free(orig_config.trigger_pre_post_duration_array);
        free(orig_config.trigger_value_array);
        if (orig_config.error_code) {
          free(orig_config.error_string);
        }

        orig_config.read_from_shared_memory(shared_memory);

        node_address_array = std::vector<uint32_t>(orig_config.node_address_array, orig_config.node_address_array+orig_config.node_address_count);
        enabled_channels_array = std::vector<uint8_t>(orig_config.enabled_channels_array, orig_config.enabled_channels_array+orig_config.enabled_channels_count);
        trigger_enabled_array = std::vector<uint8_t>(orig_config.trigger_enabled_array, orig_config.trigger_enabled_array+orig_config.enabled_channels_count);
        trigger_pre_post_duration_array = std::vector<uint32_t>(orig_config.trigger_pre_post_duration_array, orig_config.trigger_pre_post_duration_array+(orig_config.node_address_count*2));
        trigger_value_array = std::vector<float>(orig_config.trigger_value_array, orig_config.trigger_value_array+orig_config.enabled_channels_count);
      }
    }

    ImGui::PushFont(font_roboto, 20.f);

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->Pos);
    ImGui::SetNextWindowSize(viewport->Size);

    ImGui::Begin("Sensor Settings", &window_open, flags);

    bool tab_is_open = true;
    if (ImGui::BeginTabBar("node_address_tab_bar", ImGuiTabBarFlags_Reorderable|ImGuiTabBarFlags_AutoSelectNewTabs|ImGuiTabBarFlags_FittingPolicyShrink)) {
      for (int i = 0; i < node_address_array.size(); i++) {
        ImGui::PushID(i);
        bool tab_item_created;
        if (!applying_configuration) {
          tab_item_created = ImGui::BeginTabItem(fmt::format("{}", node_address_array[i]).c_str(), reinterpret_cast<bool*>(&opened_node_address_tabs[i]), ImGuiTabItemFlags_None);
        }
        else {
          tab_item_created = ImGui::BeginTabItem(fmt::format("{}", node_address_array[i]).c_str());
        }
        
        if (tab_item_created) {
          ImGui::BeginDisabled(applying_configuration);

          ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {0.f, ImGui::GetStyle().ItemSpacing.y});
          if (ImGui::BeginTable("table1", 4, ImGuiTableFlags_Borders, {ImGui::GetWindowWidth()*0.59f, 0.f})) {
            ImGui::TableNextRow(ImGuiTableRowFlags_Headers);

            ImGui::TableNextColumn();
            ImGui::TableHeader("Channel");

            ImGui::TableNextColumn();
            ImGui::TableHeader("Description");

            ImGui::TableNextColumn();
            float enable_channel_text_width = ImGui::CalcTextSize("Enable Channel", NULL, false).x;
            ImGui::SetCursorPosX(ImGui::GetCursorPosX()+(ImGui::GetColumnWidth()/2)-(enable_channel_text_width/2));
            ImGui::TableHeader("Enable Channel");

            ImGui::TableNextColumn();
            float enable_trigger_text_width = ImGui::CalcTextSize("Enable Trigger", NULL, false).x;
            ImGui::SetCursorPosX(ImGui::GetCursorPosX()+(ImGui::GetColumnWidth()/2)-(enable_trigger_text_width/2));
            ImGui::TableHeader("Enable Trigger");

            for (int y = 0; y < channel_description_array.size(); y++) {
              ImGui::PushID(y);
              ImGui::TableNextRow();

              ImGui::TableSetColumnIndex(0);
              std::string string = channel_description_array[y];

              auto channel_string = string.substr(string.find("(")+1);
              channel_string.resize(channel_string.length()-1);

              ImGui::Text("%s", fmt::format("{}", channel_string).c_str());

              ImGui::TableSetColumnIndex(1);
              string.resize(string.find("("));
              ImGui::Text("%s", string.c_str());

              ImGui::TableSetColumnIndex(2);
              ImGui::SetCursorPosX(ImGui::GetCursorPosX()+(ImGui::GetColumnWidth()/2)-(ImGui::GetFrameHeight()/2));
              ImGui::Checkbox("##sensor enabled checkbox", reinterpret_cast<bool*>(&enabled_channels_array[(i*channel_description_array.size())+y]));

              ImGui::TableSetColumnIndex(3);
              bool channel_disabled = !enabled_channels_array[(i*channel_description_array.size())+y];
              if (channel_disabled) {
                trigger_enabled_array[(i*channel_description_array.size())+y] = 0;
              }
              ImGui::BeginDisabled(channel_disabled);
              ImGui::SetCursorPosX(ImGui::GetCursorPosX()+(ImGui::GetColumnWidth()/2)-(ImGui::GetFrameHeight()/2));
              ImGui::Checkbox("##trigger enabled checkbox", reinterpret_cast<bool*>(&trigger_enabled_array[(i*channel_description_array.size())+y]));
              ImGui::EndDisabled();

              ImGui::PopID();
            }

            ImGui::EndTable();
          }
          ImGui::SameLine();
          float previous_table_height = ImGui::GetItemRectSize().y;
          if (ImGui::BeginTable("table2", 2, ImGuiTableFlags_BordersInner|ImGuiTableFlags_BordersOuterH, {ImGui::GetWindowWidth()*0.3f, 0.f})) {
            ImGui::TableNextRow(ImGuiTableRowFlags_Headers);

            ImGui::TableNextColumn();
            float trigger_pre_duration_width = ImGui::CalcTextSize("Trigger Pre Duration (ms)", NULL, false).x;
            if (ImGui::GetColumnWidth() > trigger_pre_duration_width) {
              ImGui::SetCursorPosX(ImGui::GetCursorPosX()+(ImGui::GetColumnWidth()/2)-(trigger_pre_duration_width/2));
              ImGui::TableHeader("Trigger Pre Duration (ms)");
            }
            else {
              ImGui::TableHeader("  Trigger Pre Duration (ms)");
            }

            ImGui::TableNextColumn();
            float trigger_post_duration_width = ImGui::CalcTextSize("Trigger Post Duration (ms)", NULL, false).x;
            if (ImGui::GetColumnWidth() > trigger_post_duration_width) {
              ImGui::SetCursorPosX(ImGui::GetCursorPosX()+(ImGui::GetColumnWidth()/2)-(trigger_post_duration_width/2));
              ImGui::TableHeader("Trigger Post Duration (ms)");
            }
            else {
              ImGui::TableHeader(" Trigger Post Duration (ms)");
            }

            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(0);

            float v_slider_width = 90.f;
            bool all_triggers_disabled = true;
            for (int z = i*channel_description_array.size();
                z < (i*channel_description_array.size())+channel_description_array.size();
                z++) 
            {
              if (trigger_enabled_array[z]) {
                all_triggers_disabled = false;
                break;
              }
            }

            ImGui::BeginDisabled(all_triggers_disabled);
            ImGui::SetCursorPosX(ImGui::GetCursorPosX()+(ImGui::GetColumnWidth()/2)-(v_slider_width/2));
            ImGui::VSliderInt("##trigger pre duration", {v_slider_width, previous_table_height-ImGui::GetFrameHeight()-3.f}, (int*)&trigger_pre_post_duration_array[i*2], 0, 15000);

            ImGui::TableSetColumnIndex(1);
            ImGui::SetCursorPosX(ImGui::GetCursorPosX()+(ImGui::GetColumnWidth()/2)-(v_slider_width/2));
            ImGui::VSliderInt("##trigger post duration", {v_slider_width, previous_table_height-ImGui::GetFrameHeight()-3.f}, (int*)&trigger_pre_post_duration_array[(i*2)+1], 0, 15000);

            ImGui::EndDisabled();

            ImGui::EndTable();
          }
          ImGui::SameLine();
          if (ImGui::BeginTable("table3", 1, ImGuiTableFlags_Borders, {ImGui::GetWindowWidth()*0.1f, 0.f})) {
            ImGui::TableNextRow(ImGuiTableRowFlags_Headers);

            ImGui::TableNextColumn();
            float enable_trigger_value_width = ImGui::CalcTextSize("Trigger Value", NULL, false).x;
            ImGui::SetCursorPosX(ImGui::GetCursorPosX()+(ImGui::GetColumnWidth()/2)-(enable_trigger_value_width/2));
            ImGui::TableHeader("Trigger Value");
            if (ImGui::BeginItemTooltip()) {
              ImGui::Text("Press Ctrl+Left Click to manually input a value");
              ImGui::EndTooltip();
            }

            for (int y = 0; y < channel_description_array.size(); y++) {
              ImGui::PushID(y);
              ImGui::TableNextRow();

              ImGui::BeginDisabled(!trigger_enabled_array[(i*channel_description_array.size())+y]);
              ImGui::TableSetColumnIndex(0);
              ImGui::PushItemWidth(ImGui::GetColumnWidth());
              ImGui::SliderFloat("##trigger value", &trigger_value_array[(i*channel_description_array.size())+y], 0.f, 2.f);
              ImGui::PopItemWidth();
              ImGui::EndDisabled();

              ImGui::PopID();
            }

            ImGui::EndTable();
          }

          ImGui::PopStyleVar();
          ImGui::EndDisabled();
          ImGui::EndTabItem();
        }

        if (!opened_node_address_tabs[i]) {
          node_address_array.erase(node_address_array.begin()+i);
          opened_node_address_tabs.erase(opened_node_address_tabs.begin()+i);

          int start_index = i*channel_description_array.size();
          int end_index   = start_index+channel_description_array.size();

          enabled_channels_array.erase(enabled_channels_array.begin()+start_index, enabled_channels_array.begin()+end_index);
          trigger_enabled_array.erase(trigger_enabled_array.begin()+start_index, trigger_enabled_array.begin()+end_index);
          trigger_value_array.erase(trigger_value_array.begin()+start_index, trigger_value_array.begin()+end_index);
          trigger_pre_post_duration_array.erase(trigger_pre_post_duration_array.begin()+(i*2), trigger_pre_post_duration_array.begin()+(i*2)+2);
          i--;
        }
        ImGui::PopID();
      }

      ImGui::BeginDisabled(applying_configuration);
      ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 5.0f);
      if (ImGui::TabItemButton("+", ImGuiTabItemFlags_Trailing)) {
        ImGui::OpenPopup("node_address_popup");
      }
      ImGui::PopStyleVar();
      ImGui::EndDisabled();

      if (input_text_width > 0 && node_address_error != 2) {
        ImGui::SetNextWindowSize({input_text_width+(ImGui::GetStyle().WindowPadding.x*2), 0.f});
      }

      if (ImGui::BeginPopup("node_address_popup", ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Type in a node address:");
        ImGui::InputText("##node address", buffer, ArrayCount(buffer), ImGuiInputTextFlags_CharsDecimal);
        input_text_width = ImGui::GetItemRectSize().x;

        switch (node_address_error) {
          case 1:
            ImGui::Text("The address bar is empty!");
            break;
          case 2:
            ImGui::Text("The node address provided has a length of %d which exceeds the maximum length of 5", invalid_address_length);
            break;
          case 3:
            ImGui::Text("Node address %s already exists!\n", already_existing_node_address);
            break;
        };

        ImGui::Dummy({0.f, 5.f});

        float ok_button_width = ImGui::CalcTextSize("Ok", NULL, false).x*2.f;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX()+ImGui::GetContentRegionAvail().x-ok_button_width);

        if (ImGui::Button("Ok", {ok_button_width, 0.f})) {
          bool invalid = false;
          if (!strcmp(buffer, "")) {
            invalid = true;
            node_address_error = 1;
          }
          else if (strlen(buffer) > 5) {
            invalid = true;
            node_address_error = 2;
            invalid_address_length = strlen(buffer);
          }
          else {
            for (int node_address : node_address_array) {
              if (!strcmp(fmt::format("{}", node_address).c_str(), buffer)) {
                invalid = true;
                node_address_error = 3;
                strcpy(already_existing_node_address, buffer);
                break;
              }
            }
          }

          if (!invalid) {
            node_address_error = 0;

            node_address_array.push_back(std::stoi(buffer));
            opened_node_address_tabs.push_back(1);

            for (int i = 0; i < channel_description_array.size(); i++) {
              enabled_channels_array.push_back(0);
              trigger_enabled_array.push_back(0);
              trigger_value_array.push_back(0);
            }

            trigger_pre_post_duration_array.push_back(0);
            trigger_pre_post_duration_array.push_back(0);

            ImGui::CloseCurrentPopup();
          }
        }

        ImGui::EndPopup();
      }

      ImGui::EndTabBar();
    }

    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 5.f);

    ImGui::PushFont(font_roboto, 23.f);

    float apply_configuration_button_width = 200.f;
    float reset_button_width = 85.f;
    float config_button_height = 35.f;
    float button_extra_padding = 25.f;

    ImGui::SetCursorPosX(ImGui::GetWindowWidth()-apply_configuration_button_width-ImGui::GetStyle().ItemSpacing.x-reset_button_width-button_extra_padding);
    ImGui::SetCursorPosY(ImGui::GetWindowHeight()-config_button_height-button_extra_padding);

    ImGui::BeginDisabled(applying_configuration);
    if (ImGui::Button("Apply Configuration", {apply_configuration_button_width, config_button_height})) {
      bool invalid_config = false;

      invalid_config_string.clear();

      for (int i = 0; i < trigger_value_array.size(); i++) {
        if (trigger_value_array[i] < 0.f) {
          invalid_config = true;

          auto channel_description_string = channel_description_array[i%channel_description_array.size()];
          auto channel_string = channel_description_string.substr(channel_description_string.find("(")+1);
          channel_string.resize(channel_string.length()-1);

          invalid_config_string.append(fmt::format("Node {} at {} has an invalid trigger value of {}\n", node_address_array[(int)i/channel_description_array.size()], channel_string, trigger_value_array[i]));
        }
      }

      if (invalid_config) {
        ImVec2 center = ImGui::GetMainViewport()->GetCenter();
        ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

        ImGui::OpenPopup("Invalid Configuration");
      }
      else {
        applying_configuration = true;

        free(orig_config.node_address_array);
        free(orig_config.enabled_channels_array);
        free(orig_config.trigger_enabled_array);
        free(orig_config.trigger_pre_post_duration_array);
        free(orig_config.trigger_value_array);

        orig_config.node_address_array = (uint32_t*)malloc(sizeof(*orig_config.node_address_array)*node_address_array.size());
        orig_config.enabled_channels_array = (uint8_t*)malloc(sizeof(*orig_config.enabled_channels_array)*enabled_channels_array.size());
        orig_config.trigger_enabled_array = (uint8_t*)malloc(sizeof(*orig_config.trigger_enabled_array)*trigger_enabled_array.size());
        orig_config.trigger_pre_post_duration_array = (uint32_t*)malloc(sizeof(*orig_config.trigger_pre_post_duration_array)*trigger_pre_post_duration_array.size());
        orig_config.trigger_value_array = (float*)malloc(sizeof(*orig_config.trigger_value_array)*trigger_value_array.size());

        orig_config.node_address_count = node_address_array.size();
        orig_config.enabled_channels_count = enabled_channels_array.size();

        std::copy(node_address_array.begin(), node_address_array.end(), orig_config.node_address_array);
        std::copy(enabled_channels_array.begin(), enabled_channels_array.end(), orig_config.enabled_channels_array);
        std::copy(trigger_enabled_array.begin(), trigger_enabled_array.end(), orig_config.trigger_enabled_array);
        std::copy(trigger_pre_post_duration_array.begin(), trigger_pre_post_duration_array.end(), orig_config.trigger_pre_post_duration_array);
        std::copy(trigger_value_array.begin(), trigger_value_array.end(), orig_config.trigger_value_array);

        fmt::print("======================\nAPPLYING CONFIGURATION\n");
        fmt::print("node_address_count: {}\n", orig_config.node_address_count);
        for (int i = 0; i < orig_config.node_address_count; i++) {
          fmt::print("\tnode_address_array[{}]: {}\n", i, orig_config.node_address_array[i]);
        } 
        fmt::print("num_of_channels_available: {}\n", orig_config.num_of_channels_available);
        for (int i = 0; i < orig_config.num_of_channels_available; i++) {
          fmt::print("\tchannel_description_array[{}]: {}\n", i, orig_config.channel_description_array[i]);
        } 
        fmt::print("enabled_channels_array: {}\n", std::span<uint8_t>(orig_config.enabled_channels_array, orig_config.enabled_channels_count));
        fmt::print("trigger_enabled_array: {}\n", std::span<uint8_t>(orig_config.trigger_enabled_array, orig_config.enabled_channels_count));
        fmt::print("trigger_pre_post_duration_array: {}\n", std::span<uint32_t>(orig_config.trigger_pre_post_duration_array, orig_config.node_address_count*2));
        fmt::print("trigger_value_array: {}\n", std::span<float>(orig_config.trigger_value_array, orig_config.enabled_channels_count));

        fmt::print("======================\n");

        orig_config.write_to_shared_memory(shared_memory);
        clear_client_sem();
        sem_post(server_sem);
      }
    }

    if (!saw_error_message && orig_config.error_code) {
      saw_error_message = true;
      orig_config.error_code = 0;

      orig_config.write_to_shared_memory(shared_memory);

      ImVec2 center = ImGui::GetMainViewport()->GetCenter();
      ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

      ImGui::OpenPopup("Applying Configuration Error");
      ImGui::SetNextWindowSize({400.f, 0.f});
    }

    ImGui::PushStyleColor(ImGuiCol_PopupBg, (ImVec4)ImColor(148, 24, 45));
    ImGui::PushStyleColor(ImGuiCol_TitleBgActive, (ImVec4)ImColor(103, 16, 31));
    ImGui::PushStyleColor(ImGuiCol_Button, (ImVec4)ImColor(103, 16, 31));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, (ImVec4)ImColor(118, 19, 36));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, (ImVec4)ImColor(118, 19, 36));
    if (ImGui::BeginPopupModal("Applying Configuration Error", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
      ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.f);

      ImGui::Text("Error Applying Configuration\n");
      ImGui::Text("%s", orig_config.error_string);

      float ok_button_width = ImGui::CalcTextSize("Ok", NULL, false).x*2.f;
      ImGui::SetCursorPosX(ImGui::GetCursorPosX()+ImGui::GetContentRegionAvail().x-ok_button_width);

      if (ImGui::Button("Ok", {ok_button_width, 0.f})) {
        ImGui::CloseCurrentPopup();
      }

      ImGui::PopStyleVar();

      ImGui::EndPopup();
    }
    ImGui::PopStyleColor();
    ImGui::PopStyleColor();
    ImGui::PopStyleColor();
    ImGui::PopStyleColor();
    ImGui::PopStyleColor();

    if (ImGui::BeginPopupModal("Invalid Configuration", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
      ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.f);

      ImGui::Text("%s\n", invalid_config_string.c_str());
      ImGui::Text("Trigger Values must be >= 0");

      float ok_button_width = ImGui::CalcTextSize("Ok", NULL, false).x*2.f;
      ImGui::SetCursorPosX(ImGui::GetCursorPosX()+ImGui::GetContentRegionAvail().x-ok_button_width);

      if (ImGui::Button("Ok", {ok_button_width, 0.f})) {
        ImGui::CloseCurrentPopup();
      }

      ImGui::PopStyleVar();

      ImGui::EndPopup();
    }

    ImGui::SameLine();
    if (ImGui::Button("Reset", {reset_button_width, config_button_height})) {
      node_address_array = std::vector<uint32_t>(orig_config.node_address_array, orig_config.node_address_array+orig_config.node_address_count);
      enabled_channels_array = std::vector<uint8_t>(orig_config.enabled_channels_array, orig_config.enabled_channels_array+orig_config.enabled_channels_count);
      trigger_enabled_array = std::vector<uint8_t>(orig_config.trigger_enabled_array, orig_config.trigger_enabled_array+orig_config.enabled_channels_count);
      trigger_pre_post_duration_array = std::vector<uint32_t>(orig_config.trigger_pre_post_duration_array, orig_config.trigger_pre_post_duration_array+(orig_config.node_address_count*2));
      trigger_value_array = std::vector<float>(orig_config.trigger_value_array, orig_config.trigger_value_array+orig_config.enabled_channels_count);
    }

    ImGui::EndDisabled();

    ImGui::PopFont();

    ImGui::PopStyleVar();

    if (applying_configuration) {
      ImGui::SetCursorPosX(40.f);
      ImGui::SetCursorPosY(ImGui::GetWindowHeight()-70.f);

      nextdot -= 0.07f;

      ImSpinner::Spinner<ImSpinner::e_st_dots>("SpinnerDots", ImSpinner::Radius{16}, ImSpinner::Thickness{4}, ImSpinner::Color{ImSpinner::white}, ImSpinner::FloatPtr{&nextdot}, ImSpinner::Speed{velocity}, ImSpinner::Dots{12}, ImSpinner::MinThickness{-1.f}, ImSpinner::Mode{0});

      ImGui::SameLine();
      ImGui::SetCursorPosX(30.f+widget_size);
      ImGui::SetCursorPosY(ImGui::GetWindowHeight()-70.f+2.f);
      ImGui::PushFont(font_roboto, 24.f);
      ImGui::Text("Applying Configuration Changes");
      ImGui::PopFont();
    }

    ImGui::End();

    //ImGui::ShowDemoWindow(&demo_window);

    ImGui::PopFont();

    ImGui::Render();
    int display_w, display_h;
    glfwGetFramebufferSize(window, &display_w, &display_h);
    glViewport(0, 0, display_w, display_h);
    glClearColor(clear_color.x * clear_color.w, clear_color.y * clear_color.w, clear_color.z * clear_color.w, clear_color.w);
    glClear(GL_COLOR_BUFFER_BIT);

    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    glfwSwapBuffers(window);
  }

  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext();

  glfwDestroyWindow(window);
  glfwTerminate();

  munmap(shared_memory, SHARED_MEMORY_SIZE);

  sem_close(server_sem);
  sem_close(client_sem);
}
