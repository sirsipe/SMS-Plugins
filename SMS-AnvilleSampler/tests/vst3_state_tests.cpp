// Compile the maintained wrapper itself; only the host and view are simulated.
#include "src/DistrhoPluginVST3.cpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <map>
#include <string>
#include <utility>
#include <vector>

static_assert(DISTRHO_PLUGIN_HAS_UI == 1 && DPF_VST3_USES_SEPARATE_CONTROLLER == 0,
              "exercise Anville's UI-enabled combined VST3 controller");

namespace {

void check(const bool condition, const char* const message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

template<class T> T& object(void* const self)
{
    return **static_cast<T**>(self);
}

// The deliberately non-index/alphabetical order also checks existing map order.
constexpr std::array<const char*, 3> keys = {"z_visible", "a_pcm", "m_ui"};
constexpr size_t pcmIndex = 1;

size_t stateIndex(const char* const key)
{
    for (size_t i = 0; i < keys.size(); ++i)
        if (std::strcmp(key, keys[i]) == 0)
            return i;
    check(false, "wrapper queries a registered state key");
    return 0;
}

using StateValues = std::array<std::string, keys.size()>;

struct Attributes final : v3_attribute_list_cpp {
    Attributes* handle = this;
    std::map<std::string, int64_t> integers;
    std::map<std::string, std::vector<int16_t>> strings;

    Attributes()
    {
        attrlist.set_int = setInt;
        attrlist.set_string = setString;
    }

    static v3_result V3_API setInt(void* const self, const char* const key, const int64_t value)
    {
        object<Attributes>(self).integers[key] = value;
        return V3_OK;
    }

    static v3_result V3_API setString(void* const self, const char* const key, const int16_t* const value)
    {
        const int16_t* end = value;
        while (*end != 0)
            ++end;
        object<Attributes>(self).strings[key].assign(value, end);
        return V3_OK;
    }
};

struct Message final : v3_message_cpp {
    Message* handle = this;
    uint32_t references = 1;
    std::string id;
    Attributes attributes;

    explicit Message(const char* const messageId = "") : id(messageId)
    {
        ref = addRef;
        unref = release;
        msg.get_message_id = getId;
        msg.set_message_id = setId;
        msg.get_attributes = getAttributes;
    }

    v3_message** interface() { return reinterpret_cast<v3_message**>(&handle); }

    static uint32_t V3_API addRef(void* const self) { return ++object<Message>(self).references; }
    static uint32_t V3_API release(void* const self)
    {
        auto& message = object<Message>(self);
        if (--message.references != 0)
            return message.references;
        delete &message;
        return 0;
    }
    static const char* V3_API getId(void* const self) { return object<Message>(self).id.c_str(); }
    static void V3_API setId(void* const self, const char* const id) { object<Message>(self).id = id; }
    static v3_attribute_list** V3_API getAttributes(void* const self)
    {
        return reinterpret_cast<v3_attribute_list**>(&object<Message>(self).attributes.handle);
    }
};

struct Host final : v3_host_application_cpp {
    Host* handle = this;
    Host() { app.create_instance = createInstance; }
    v3_host_application** interface() { return reinterpret_cast<v3_host_application**>(&handle); }

    static v3_result V3_API createInstance(void*, v3_tuid cid, v3_tuid iid, void** const result)
    {
        check(v3_tuid_match(cid, v3_message_iid) && v3_tuid_match(iid, v3_message_iid),
              "wrapper requests host messages");
        auto* const message = new Message;
        *result = message->interface();
        return V3_OK;
    }
};

std::string ascii(const std::vector<int16_t>& value)
{
    std::string result;
    for (const int16_t character : value) {
        check(character >= 0 && character < 128, "fixture state messages retain ASCII contents");
        result += static_cast<char>(character);
    }
    return result;
}

struct View final : v3_connection_point_cpp {
    View* handle = this;
    std::vector<std::pair<std::string, std::string>> states;
    size_t readyCount = 0;

    View() { point.notify = notify; }
    v3_connection_point** interface() { return reinterpret_cast<v3_connection_point**>(&handle); }
    void clear() { states.clear(); readyCount = 0; }

    static v3_result V3_API notify(void* const self, v3_message** const incoming)
    {
        auto& view = object<View>(self);
        auto& message = object<Message>(incoming);
        if (message.id == "ready") {
            ++view.readyCount;
            return V3_OK;
        }
        check(message.id == "state-set", "zero-parameter fixture receives only state and readiness");
        const auto& attributes = message.attributes;
        check(attributes.integers.at("__dpf_msg_target__") == 2, "state messages target the view");
        const auto key = ascii(attributes.strings.at("key"));
        check(key != keys[pcmIndex], "DSP-only PCM never crosses the view connection");
        const auto value = ascii(attributes.strings.at("value"));
        check(attributes.integers.at("key:length") == static_cast<int64_t>(key.size()) &&
              attributes.integers.at("value:length") == static_cast<int64_t>(value.size()),
              "state message lengths and contents agree");
        view.states.emplace_back(key, value);
        return V3_OK;
    }
};

struct Stream final : v3_bstream_cpp {
    Stream* handle = this;
    std::vector<char> bytes;
    size_t cursor = 0;

    Stream()
    {
        stream.read = read;
        stream.write = write;
    }
    v3_bstream** interface() { return reinterpret_cast<v3_bstream**>(&handle); }

    static v3_result V3_API read(void* const self, void* const buffer, const int32_t size, int32_t* const count)
    {
        auto& data = object<Stream>(self);
        *count = static_cast<int32_t>(std::min<size_t>(size, data.bytes.size() - data.cursor));
        std::memcpy(buffer, data.bytes.data() + data.cursor, *count);
        data.cursor += *count;
        return V3_OK;
    }
    static v3_result V3_API write(void* const self, void* const buffer, const int32_t size, int32_t* const count)
    {
        auto& data = object<Stream>(self);
        // Real hosts may accept short writes; keep the complete-state loop covered.
        *count = std::min(size, 4093);
        const auto* const begin = static_cast<const char*>(buffer);
        data.bytes.insert(data.bytes.end(), begin, begin + *count);
        return V3_OK;
    }
};

} // namespace

START_NAMESPACE_DISTRHO

class StateFixture final : public Plugin {
public:
    StateValues values = {"current visible settings", std::string(1024 * 1024, 'A'), "current UI settings"};
    mutable std::array<size_t, keys.size()> reads{};

    StateFixture() : Plugin(0, 0, keys.size()) {}
    const char* getLabel() const override { return "state-test"; }
    const char* getMaker() const override { return "SMS"; }
    const char* getLicense() const override { return "AGPL-3.0-only"; }
    uint32_t getVersion() const override { return 1; }

    void initState(const uint32_t index, State& state) override
    {
        state.key = keys[index];
        state.defaultValue = "stale default";
        state.hints = index == pcmIndex ? kStateIsOnlyForDSP | kStateIsBase64Blob
                                       : index == 2 ? kStateIsOnlyForUI : 0;
    }
    String getState(const char* const key) const override
    {
        const auto index = stateIndex(key);
        ++reads[index];
        return String(values[index].c_str());
    }
    void setState(const char* const key, const char* const value) override { values[stateIndex(key)] = value; }
    void run(const float**, float**, uint32_t) override {}
};

Plugin* createPlugin() { return new StateFixture; }

// GUI creation is outside this test. The controller's real init message and
// state conversion/transmission run through the fake connection above.
v3_plugin_view** dpf_plugin_view_create(v3_host_application**, void*, double) { return nullptr; }

END_NAMESPACE_DISTRHO

namespace {

void checkVisibleState(const View& view, const StateValues& values)
{
    const std::vector<std::pair<std::string, std::string>> expected = {
        {keys[2], values[2]}, {keys[0], values[0]}};
    check(view.states == expected, "regular and UI-only state arrive exactly once in existing map order");
}

void open(DISTRHO::PluginVst3& wrapper, View& view, DISTRHO::StateFixture& fixture)
{
    fixture.reads.fill(0);
    view.clear();
    wrapper.ctrl2view_connect(view.interface());
    Message init("init");
    check(wrapper.ctrl2view_notify(init.interface()) == V3_OK, "editor initialization succeeds");
    check(fixture.reads[pcmIndex] == 0, "editor initialization never serializes DSP-only PCM");
    check(fixture.reads[0] == 1 && fixture.reads[2] == 1, "editor initialization refreshes visible state");
    checkVisibleState(view, fixture.values);
    check(view.readyCount == 1, "editor initialization completes its ready handshake");
}

void close(DISTRHO::PluginVst3& wrapper)
{
    Message closeMessage("close");
    check(wrapper.ctrl2view_notify(closeMessage.interface()) == V3_OK, "editor closes normally");
    wrapper.ctrl2view_disconnect();
}

} // namespace

int main()
{
    using namespace DISTRHO;
    d_nextSampleRate = 48000;
    d_nextBufferSize = 512;
    Host host;
    View view;
    PluginVst3 source(host.interface(), true);
    auto& original = *static_cast<StateFixture*>(source.getInstancePointer());
    const auto savedValues = original.values;

    open(source, view, original);
    close(source);

    Stream saved;
    original.reads.fill(0);
    check(source.getState(saved.interface()) == V3_OK, "host saves complete state");
    check(original.reads == std::array<size_t, 3>{1, 1, 1}, "host save still queries every state value");
    check(saved.bytes.size() > savedValues[pcmIndex].size(), "host state retains the full PCM payload");

    // The save populated fStateMap with PCM. Reopening must skip that cached
    // value as well as avoid asking the DSP to encode another copy.
    original.values[0] = "visible settings changed after save";
    original.values[2] = "UI settings changed after save";
    open(source, view, original);
    close(source);

    PluginVst3 target(host.interface(), true);
    auto& restored = *static_cast<StateFixture*>(target.getInstancePointer());
    restored.values.fill("replaced before closed restoration");
    view.clear();
    check(target.setState(saved.interface()) == V3_OK, "host restores with editor closed");
    check(restored.values == savedValues, "closed-editor restoration retains all state exactly");
    check(view.states.empty() && view.readyCount == 0, "closed restoration sends no view messages");

    open(target, view, restored);
    restored.values.fill("replaced before open restoration");
    view.clear();
    saved.cursor = 0;
    check(target.setState(saved.interface()) == V3_OK, "host restores with editor open");
    check(restored.values == savedValues, "open-editor restoration retains all state exactly");
    checkVisibleState(view, savedValues);
    close(target);

    std::cout << "VST3 state transport tests passed\n";
}
