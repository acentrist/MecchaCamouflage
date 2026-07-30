#include <meccha/application/config_codec.hpp>
#include <meccha/application/config_store.hpp>

#include <meccha/core/config.hpp>

#include <cstddef>
#include <expected>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string_view>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace
{
auto expect(bool condition, std::string_view message) -> bool
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        return false;
    }
    return true;
}

auto replace_once(
    std::string value,
    std::string_view from,
    std::string_view to) -> std::string
{
    const auto position = value.find(from);
    if (position != std::string::npos)
    {
        value.replace(position, from.size(), to);
    }
    return value;
}

class FakeAtomicTextStorage final
    : public meccha::application::AtomicTextStorage
{
public:
    auto read_text(std::string_view name, std::size_t maximum_bytes)
        -> std::expected<
            std::optional<std::string>,
            meccha::application::TextStorageError> override
    {
        reads.emplace_back(name);
        const auto found = files.find(std::string{name});
        if (found == files.end())
        {
            return std::nullopt;
        }
        if (found->second.size() > maximum_bytes)
        {
            return std::unexpected(meccha::application::TextStorageError{
                meccha::application::TextStorageErrorCode::TooLarge,
                "fake oversized file",
            });
        }
        return found->second;
    }

    auto write_text_atomic(std::string_view name, std::string_view text)
        -> std::expected<void, meccha::application::TextStorageError> override
    {
        writes.emplace_back(name);
        if (fail_write)
        {
            return std::unexpected(meccha::application::TextStorageError{
                meccha::application::TextStorageErrorCode::Io,
                "injected write failure",
            });
        }
        files.insert_or_assign(std::string{name}, std::string{text});
        return {};
    }

    std::unordered_map<std::string, std::string> files{};
    std::vector<std::string> reads{};
    std::vector<std::string> writes{};
    bool fail_write{};
};
} // namespace

auto main() -> int
{
    using namespace meccha;

    auto passed = true;
    auto config = core::ApplicationConfig{};
    config.ui.language = "ja";
    config.paint.front_mode = core::RegionMode::Fill;
    config.image_paint.body = core::BodyProfile::Fukuyoka;
    config.esp.scope = core::EspScope::Hunter;

    const auto encoded = application::encode_config(config);
    passed &= expect(encoded.has_value(), "valid config did not encode");
    if (!encoded)
    {
        return 1;
    }

    passed &= expect(
        encoded->find(R"("toggle_ui":"F9")") != std::string::npos &&
            encoded->find(R"("front_mode":"fill")") !=
                std::string::npos &&
            encoded->find(R"("body":"fukuyoka")") !=
                std::string::npos &&
            encoded->find(R"("scope":"hunter")") !=
                std::string::npos,
        "persisted enum spellings are not stable strings");
    passed &= expect(
        encoded->find("\"layers\"") == std::string::npos &&
            encoded->find("\"source_bytes\"") == std::string::npos &&
            encoded->find("\"webview\"") == std::string::npos &&
            encoded->find("\"bridge\"") == std::string::npos,
        "config contains image data or a retired architecture field");

    const auto decoded = application::decode_config(*encoded);
    passed &= expect(
        decoded.has_value() && *decoded == config,
        "valid config did not round trip exactly");
    const auto encoded_again = application::encode_config(*decoded);
    passed &= expect(
        encoded_again.has_value() && *encoded_again == *encoded,
        "config encoding is not deterministic");

    const auto unknown = replace_once(
        *encoded,
        R"("schema_version":1)",
        R"("schema_version":1,"legacy_window":{})");
    const auto unknown_result = application::decode_config(unknown);
    passed &= expect(
        !unknown_result &&
            unknown_result.error().code ==
                application::ConfigCodecErrorCode::MalformedJson,
        "unknown config fields were accepted");

    const auto missing_result =
        application::decode_config(R"({"schema_version":1})");
    passed &= expect(
        !missing_result &&
            missing_result.error().code ==
                application::ConfigCodecErrorCode::MalformedJson,
        "missing required config fields were accepted");

    const auto old_schema = replace_once(
        *encoded,
        R"("schema_version":1)",
        R"("schema_version":0)");
    const auto old_schema_result =
        application::decode_config(old_schema);
    passed &= expect(
        !old_schema_result &&
            old_schema_result.error().code ==
                application::ConfigCodecErrorCode::UnsupportedSchema,
        "non-v2 schema did not get an explicit rejection");

    const auto duplicate_key = replace_once(
        *encoded,
        R"("language":"ja")",
        R"("language":"en","language":"ja")");
    const auto duplicate_result =
        application::decode_config(duplicate_key);
    passed &= expect(
        !duplicate_result &&
            duplicate_result.error().code ==
                application::ConfigCodecErrorCode::MalformedJson,
        "duplicate object keys were accepted");

    const auto escaped_duplicate_key = replace_once(
        *encoded,
        R"("language":"ja")",
        R"("language":"en","\u006canguage":"ja")");
    const auto escaped_duplicate_result =
        application::decode_config(escaped_duplicate_key);
    passed &= expect(
        !escaped_duplicate_result &&
            escaped_duplicate_result.error().code ==
                application::ConfigCodecErrorCode::MalformedJson,
        "escaped duplicate object keys were accepted");

    const auto commented = replace_once(
        *encoded,
        R"("schema_version":1)",
        "\"schema_version\":1 // comment");
    const auto commented_result =
        application::decode_config(commented);
    passed &= expect(
        !commented_result &&
            commented_result.error().code ==
                application::ConfigCodecErrorCode::MalformedJson,
        "JSON comments were accepted");

    const auto invalid_scale = replace_once(
        *encoded,
        R"("scale":1)",
        R"("scale":99)");
    const auto invalid_scale_result =
        application::decode_config(invalid_scale);
    passed &= expect(
        !invalid_scale_result &&
            invalid_scale_result.error().code ==
                application::ConfigCodecErrorCode::InvalidValue &&
            invalid_scale_result.error().fields ==
                std::vector{core::ConfigurationField::UiScale},
        "domain-invalid config was accepted");

    const auto trailing_result =
        application::decode_config(*encoded + " trailing");
    passed &= expect(
        !trailing_result &&
            trailing_result.error().code ==
                application::ConfigCodecErrorCode::MalformedJson,
        "trailing non-whitespace content was accepted");

    const auto oversized_result = application::decode_config(
        std::string(application::MaximumConfigBytes + 1U, ' '));
    passed &= expect(
        !oversized_result &&
            oversized_result.error().code ==
                application::ConfigCodecErrorCode::TooLarge,
        "oversized config was accepted");

    auto invalid_for_write = config;
    invalid_for_write.ui.scale = 3.0;
    const auto invalid_write =
        application::encode_config(invalid_for_write);
    passed &= expect(
        !invalid_write &&
            invalid_write.error().code ==
                application::ConfigCodecErrorCode::InvalidValue,
        "invalid config was serialized");

    passed &= expect(
        application::v2_data_root(
            std::filesystem::path{"local-app-data"}) ==
            std::filesystem::path{
                "local-app-data/MecchaCamouflage/v2"},
        "v2 data root is not isolated from v1 data");

    auto storage = FakeAtomicTextStorage{};
    auto store = application::ConfigStore{storage};
    const auto missing = store.load();
    passed &= expect(
        missing && missing->source ==
                       application::ConfigLoadSource::Defaults &&
            missing->config == core::ApplicationConfig{} &&
            storage.reads == std::vector<std::string>{"config.json"} &&
            storage.writes.empty(),
        "missing config did not return defaults without writing");

    const auto saved = store.save(config);
    passed &= expect(
        saved.has_value() &&
            storage.writes ==
                std::vector<std::string>{"config.json"},
        "valid config was not atomically delegated once");
    const auto loaded = store.load();
    passed &= expect(
        loaded && loaded->source ==
                      application::ConfigLoadSource::Persisted &&
            loaded->config == config,
        "persisted config did not load");

    storage.files["config.json"] = "{broken";
    const auto malformed_load = store.load();
    passed &= expect(
        !malformed_load &&
            malformed_load.error().code ==
                application::ConfigStoreErrorCode::Codec &&
            storage.files["config.json"] == "{broken",
        "malformed config was mutated or silently defaulted");

    storage.files["config.json"] = *encoded;
    storage.fail_write = true;
    auto replacement = config;
    replacement.ui.language = "de";
    const auto failed_save = store.save(replacement);
    passed &= expect(
        !failed_save &&
            failed_save.error().code ==
                application::ConfigStoreErrorCode::Storage &&
            storage.files["config.json"] == *encoded,
        "failed save replaced the previous valid config");

    storage.fail_write = false;
    const auto writes_before_invalid = storage.writes.size();
    const auto invalid_store = store.save(invalid_for_write);
    passed &= expect(
        !invalid_store &&
            invalid_store.error().code ==
                application::ConfigStoreErrorCode::Codec &&
            storage.writes.size() == writes_before_invalid,
        "invalid config reached storage");

    if (passed)
    {
        std::cout << "PASS config_persistence\n";
    }
    return passed ? 0 : 1;
}
