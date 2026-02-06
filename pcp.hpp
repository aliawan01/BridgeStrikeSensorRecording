#pragma once

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

#include <mscl/mscl.h>

#pragma GCC diagnostic pop

#include <arrow/api.h>
#include <arrow/io/api.h>
#include <parquet/arrow/writer.h>

#include <azure/core.hpp>
#include <azure/identity/default_azure_credential.hpp>
#include <azure/storage/blobs.hpp>

#include <chrono>
#include <thread>
#include <unordered_map>
#include <string>
#include <array>
#include <vector>
#include <filesystem>
#include <mutex>
#include <semaphore>
#include <cmath>
#include <algorithm>
#include <span>

#include <fmt/base.h>
#include <fmt/core.h>
#include <fmt/chrono.h>
#include <fmt/ranges.h>

extern "C" {
#include <curl/curl.h>
#include <time.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <semaphore.h>
}
