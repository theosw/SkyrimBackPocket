#include "pch.h"

#include "config.h"
#include "disenchant_filter.h"
#include "inventory_filter.h"
#include "persistence.h"
#include "plugin_version.h"

namespace back_pocket {
namespace {
struct plugin_state {
  config::settings configuration;
  pocket pocket_state;
  bool ready = false;
};

plugin_state& state() {
  static plugin_state instance;
  return instance;
}

void initialize_log() {
  std::vector<spdlog::sink_ptr> sinks;
#ifndef NDEBUG
  sinks.push_back(std::make_shared<spdlog::sinks::msvc_sink_mt>());
#endif

  std::optional<std::filesystem::path> path = logger::log_directory();
  if (!path.has_value()) {
    SKSE::stl::report_and_fail("Back Pocket could not locate the SKSE log directory");
  }
  *path /= std::format("{}.log", plugin_version::name);
  sinks.push_back(std::make_shared<spdlog::sinks::basic_file_sink_mt>(path->string(), true));

  auto log = std::make_shared<spdlog::logger>("BackPocket", sinks.begin(), sinks.end());
#ifndef NDEBUG
  log->set_level(spdlog::level::debug);
#else
  log->set_level(spdlog::level::info);
#endif
  log->flush_on(spdlog::level::info);
  spdlog::set_default_logger(std::move(log));
  spdlog::set_pattern("[%H:%M:%S.%e] [%l] %v");
}

void on_skse_message(SKSE::MessagingInterface::Message* message) {
  if (message == nullptr) {
    return;
  }
  plugin_state& current = state();
  if (message->type == SKSE::MessagingInterface::kNewGame) {
    current.pocket_state.clear();
    logger::info("new game: Back Pocket cleared");
    return;
  }
  if (message->type != SKSE::MessagingInterface::kDataLoaded) {
    return;
  }

  current.configuration = config::load();
  const bool inventory_ready =
      inventory_filter::install(current.pocket_state, current.configuration);
  const bool disenchant_ready =
      inventory_ready &&
      disenchant_filter::install(current.pocket_state,
                                 current.configuration.hide_pocketed_from_disenchanting);
  current.ready = inventory_ready && disenchant_ready;
  logger::info("PLUGIN_READY inventory_filter={} disenchant_filter={}", inventory_ready,
               disenchant_ready);
  if (!current.ready) {
    RE::DebugNotification("Back Pocket failed to initialize; check BackPocket.log");
  }
}
} // namespace
} // namespace back_pocket

extern "C" DLLEXPORT bool SKSEAPI SKSEPlugin_Load(const SKSE::LoadInterface* skse) {
  back_pocket::initialize_log();
  logger::info("{} v{} loading", back_pocket::plugin_version::name,
               back_pocket::plugin_version::version_string);

  const REL::Version runtime = skse->RuntimeVersion();
  if (!back_pocket::plugin_version::supports_runtime(runtime)) {
    logger::critical("unsupported Skyrim runtime {}; this build supports {} and {}", runtime,
                     back_pocket::plugin_version::se_runtime,
                     back_pocket::plugin_version::ae_runtime);
    return false;
  }

  SKSE::Init(skse);
  if (!back_pocket::persistence::install(back_pocket::state().pocket_state)) {
    logger::critical("failed to register SKSE serialization");
    return false;
  }
  if (!SKSE::GetMessagingInterface()->RegisterListener(back_pocket::on_skse_message)) {
    logger::critical("failed to register the SKSE message listener");
    return false;
  }

  logger::info("plugin loaded; waiting for kDataLoaded");
  return true;
}

extern "C" DLLEXPORT constinit auto SKSEPlugin_Version = []() noexcept {
  SKSE::PluginVersionData version;
  version.PluginName(back_pocket::plugin_version::name.data());
  version.PluginVersion(back_pocket::plugin_version::version);
  version.CompatibleVersions(
      {back_pocket::plugin_version::se_runtime, back_pocket::plugin_version::ae_runtime});
  version.HasNoStructUse();
  return version;
}();

extern "C" DLLEXPORT bool SKSEAPI SKSEPlugin_Query(const SKSE::QueryInterface* skse,
                                                   SKSE::PluginInfo* plugin_info) {
  plugin_info->name = SKSEPlugin_Version.pluginName;
  plugin_info->infoVersion = SKSE::PluginInfo::kVersion;
  plugin_info->version = SKSEPlugin_Version.pluginVersion;
  return back_pocket::plugin_version::supports_runtime(skse->RuntimeVersion());
}
