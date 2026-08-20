#include "pch.h"

#include "persistence.h"

#include "back_pocket/saved_state.h"

namespace back_pocket::persistence {
namespace {
constexpr std::uint32_t tag(const char first, const char second, const char third,
                            const char fourth) {
  return static_cast<std::uint32_t>(static_cast<unsigned char>(first)) << 24 |
         static_cast<std::uint32_t>(static_cast<unsigned char>(second)) << 16 |
         static_cast<std::uint32_t>(static_cast<unsigned char>(third)) << 8 |
         static_cast<std::uint32_t>(static_cast<unsigned char>(fourth));
}

constexpr std::uint32_t serialization_id = tag('B', 'P', 'K', 'T');
constexpr std::uint32_t forms_record_type = tag('F', 'R', 'M', 'S');
constexpr std::size_t maximum_payload_size =
    sizeof(std::uint32_t) + saved_state::maximum_entries * sizeof(form_id);

pocket*& state() {
  static pocket* value = nullptr;
  return value;
}

const char* describe_failure(const saved_state::failure failure) {
  switch (failure) {
  case saved_state::failure::none:
    return "none";
  case saved_state::failure::too_many_entries:
    return "too many entries";
  case saved_state::failure::invalid_size:
    return "invalid payload size";
  case saved_state::failure::invalid_form:
    return "invalid form";
  }
  return "unknown";
}

void on_save(SKSE::SerializationInterface* serialization) {
  if (serialization == nullptr || state() == nullptr) {
    logger::error("save skipped because persistence is not initialized");
    return;
  }

  const std::vector<form_id> forms = state()->snapshot();
  saved_state::encode_result encoded = saved_state::encode(forms);
  if (encoded.error != saved_state::failure::none) {
    logger::error("Back Pocket save failed: {}", describe_failure(encoded.error));
    return;
  }
  if (!serialization->OpenRecord(forms_record_type, saved_state::current_version) ||
      !serialization->WriteRecordData(encoded.bytes.data(),
                                      static_cast<std::uint32_t>(encoded.bytes.size()))) {
    logger::error("Back Pocket save failed while writing the forms record");
    return;
  }
  logger::info("saved Back Pocket: forms={}, bytes={}", forms.size(), encoded.bytes.size());
}

void on_load(SKSE::SerializationInterface* serialization) {
  if (serialization == nullptr || state() == nullptr) {
    logger::error("load skipped because persistence is not initialized");
    return;
  }

  std::vector<form_id> resolved_forms;
  std::uint32_t type = 0;
  std::uint32_t version = 0;
  std::uint32_t length = 0;
  while (serialization->GetNextRecordInfo(type, version, length)) {
    if (type != forms_record_type) {
      logger::warn("ignoring unknown serialization record {:08X}", type);
      continue;
    }
    if (version != saved_state::current_version) {
      logger::warn("ignoring unsupported Back Pocket record version {}", version);
      continue;
    }
    if (length > maximum_payload_size) {
      logger::error("ignoring oversized Back Pocket record: {} bytes", length);
      continue;
    }

    std::vector<std::byte> bytes(length);
    if (serialization->ReadRecordData(bytes.data(), length) != length) {
      logger::error("ignoring truncated Back Pocket record");
      continue;
    }
    saved_state::decode_result decoded = saved_state::decode(bytes);
    if (decoded.error != saved_state::failure::none) {
      logger::error("ignoring invalid Back Pocket record: {}", describe_failure(decoded.error));
      continue;
    }

    for (const form_id saved_form : decoded.forms) {
      RE::FormID resolved_form = 0;
      if (serialization->ResolveFormID(saved_form, resolved_form) && resolved_form != 0) {
        resolved_forms.push_back(resolved_form);
      } else {
        logger::debug("discarded unresolved Back Pocket form {:08X}", saved_form);
      }
    }
  }

  state()->restore(resolved_forms);
  logger::info("loaded Back Pocket: forms={}", state()->size());
}

void on_revert(SKSE::SerializationInterface*) {
  if (state() != nullptr) {
    state()->clear();
  }
  logger::info("reverted Back Pocket state");
}
} // namespace

bool install(pocket& pocket_state) {
  const SKSE::SerializationInterface* serialization = SKSE::GetSerializationInterface();
  if (serialization == nullptr) {
    logger::error("SKSE serialization interface is unavailable");
    return false;
  }

  state() = &pocket_state;
  serialization->SetUniqueID(serialization_id);
  serialization->SetSaveCallback(&on_save);
  serialization->SetLoadCallback(&on_load);
  serialization->SetRevertCallback(&on_revert);
  logger::info("SKSE serialization callbacks registered");
  return true;
}
} // namespace back_pocket::persistence
