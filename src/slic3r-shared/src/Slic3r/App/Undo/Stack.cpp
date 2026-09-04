#include "Slic3r/App/Undo/Stack.hpp"

#include <ranges>
#include <span>

#include "Slic3r/App/Undo/ConfigContainerListSerialize.hpp"
#include "Slic3r/App/Undo/ModelSerialize.hpp"
#include "Slic3r/App/Undo/ObjectSelectionSerialize.hpp"
#include "Slic3r/App/Undo/BedSelectionStateSerialize.hpp"
#include "Slic3r/App/Undo/SerializedData.hpp"
#include "Slic3r/App/Undo/ToolStateSerialize.hpp"

#if defined(_WIN32)
    #include <windows.h>
#endif


namespace Slic3r::App::Undo {

class Channel
{
public:
    void add_chunk(std::size_t snapshot_index, Chunk chunk)
    {
        if (m_chunks.empty()) {
            m_intervals.push_back(snapshot_index);
            m_intervals.push_back(snapshot_index + 1);
            m_chunks.push_back(std::move(chunk));
        } else if (std::holds_alternative<ConfigContainerChunk>(chunk)){
            if (!m_chunks.empty()) {
                ConfigContainerChunk& new_chunk{std::get<ConfigContainerChunk>(chunk)};
                ConfigContainerChunk& old_chunk{std::get<ConfigContainerChunk>(m_chunks.back())};
                if (old_chunk.config_id == new_chunk.config_id) {
                    m_chunks.back() = {std::move(chunk)};
                    m_intervals.back() = snapshot_index + 1;
                } else {
                    m_intervals.push_back(snapshot_index + 1);
                    m_chunks.push_back(std::move(chunk));
                }
            } else {
                m_intervals = {0, snapshot_index + 1};
                m_chunks    = {std::move(chunk)};
            }
        } else if (chunk == m_chunks.back()) {
            m_intervals.back() = snapshot_index + 1;
        } else {
            m_intervals.push_back(snapshot_index + 1);
            m_chunks.push_back(std::move(chunk));
        }
    }

    const Chunk* get_chunk(std::size_t snapshot_index) const
    {
        const std::optional<std::size_t> chunk_index{get_chunk_index(snapshot_index)};
        if (!chunk_index) {
            return nullptr;
        }
        return &m_chunks.at(*chunk_index);
    }

    void remove_snapshots(std::size_t from, std::size_t to)
    {
        ASSERT(from >= 0);
        if (to == 0) {
            return;
        }
        for (int i{static_cast<int>(to) - 1}; i >= static_cast<int>(from); --i) {
            remove_snapshot(static_cast<std::size_t>(i));
        }
        clear_empty_intervals();
    }

    bool empty() const
    {
        return m_chunks.empty();
    }

    std::size_t memsize() const
    {
        std::size_t result{sizeof(decltype(m_intervals)::value_type) * m_intervals.capacity()};
        for (const Chunk& chunk : m_chunks) {
            std::size_t chunk_size{std::visit(
                Domain::overloaded{
                    [](const std::string& string) { return string.capacity(); },
                    [](const TriangleMeshChunk& mesh_chunk)
                    {
                        return mesh_chunk.mesh->its.memsize();
                    },
                    [](const VersionedChunk& versioned_chunk)
                    { return versioned_chunk.serialized_data.capacity(); },
                    [](const ConfigContainerChunk& config_chunk)
                    { return config_chunk.serialized_data.capacity(); }
                },
                chunk
            )};
            result += chunk_size;
        }
        return result;
    }

private:
    std::optional<std::size_t> get_chunk_index(std::size_t snapshot_index) const
    {
        if (m_intervals.empty()
            || snapshot_index < m_intervals.front()
            || snapshot_index >= m_intervals.back())
        {
            return std::nullopt;
        }

        const auto interval_end{
            std::upper_bound(m_intervals.begin(), m_intervals.end(), snapshot_index)
        };
        const auto interval_begin{std::prev(interval_end)};

        return std::distance(m_intervals.begin(), interval_begin);
    }

    void remove_snapshot(std::size_t snapshot_index)
    {
        for (std::size_t& boundary : m_intervals) {
            if (boundary > snapshot_index) {
                --boundary;
            }
        }
    }

    void clear_empty_intervals()
    {
        if (m_chunks.empty())
            return;

        std::size_t write{0};

        for (std::size_t read{0}; read < m_chunks.size(); ++read) {
            if (m_intervals[read] < m_intervals[read + 1]) {
                if (write != read) {
                    std::swap(m_chunks[write], m_chunks[read]);
                    std::swap(m_intervals[write], m_intervals[read]);
                }
                ++write;
            }
        }

        if (write == 0) {
            m_chunks.clear();
            m_intervals.clear();
            return;
        }

        m_intervals[write] = m_intervals.back();

        m_chunks.resize(write);
        m_intervals.resize(write + 1);
    }

    std::vector<Chunk> m_chunks;
    std::vector<std::size_t> m_intervals;
};

class SerializedDataStack
{
public:
    void save_snapshot(std::vector<SerializedData> data, Biz::UndoSnapshotType type)
    {
        if (!m_snapshots.empty()) {
            ASSERT(m_snapshots.back().serialized_data.size() == data.size());
        }
        Snapshot snapshot;
        snapshot.serialized_data.resize(data.size());
        snapshot.type = type;

        for (std::size_t i{}; i < data.size(); ++i) {
            SerializedData& entry{data[i]};
            ASSERT(!entry.serialized_data.empty());
            std::vector<ChannelId> used_channels;
            for (auto& [channel_id, chunk] : entry.separate_chunks) {
                Channel& channel{m_channels[channel_id]};
                channel.add_chunk(m_end_snapshot_index, std::move(chunk));
                used_channels.push_back(channel_id);
            }
            snapshot.serialized_data.at(
                i
            ) = {std::move(entry.serialized_data), std::move(used_channels)};
        }

        m_snapshots.push_back(std::move(snapshot));
        m_end_snapshot_index++;

        assert_consistency(m_channels, m_snapshots);
    }

    std::vector<SerializedData> get_snapshot_data(const Snapshot& snapshot) const
    {
        const std::size_t snapshot_index{id_to_index(snapshot.id)};
        std::vector<SerializedData> result;
        for (const auto& [serialize_data, used_channels] : snapshot.serialized_data) {
            std::map<ChannelId, Chunk> separate_chunks;
            for (ChannelId channel_id : used_channels) {
                const Chunk* chunk{m_channels.at(channel_id).get_chunk(snapshot_index)};
                ASSERT(chunk);
                separate_chunks[channel_id] = *chunk;
            }
            result.push_back(SerializedData{std::move(separate_chunks), serialize_data});
        }

        return result;
    }

    void remove_snapshots_range(std::size_t id_first, std::size_t id_last)
    {
        const std::size_t interval_begin{id_to_index(id_first)};
        const std::size_t interval_end{id_to_index(id_last) + 1};
        remove_snapshots(interval_begin, interval_end);
    }

    void pop_front_n(std::size_t n)
    {
        const std::size_t interval_begin{0};
        const std::size_t interval_end{std::min(n, m_snapshots.size())};
        remove_snapshots(interval_begin, interval_end);
    }

    void pop_back_n(std::size_t n)
    {
        const std::size_t interval_begin{static_cast<std::size_t>(
            std::max(int64_t{0}, static_cast<int64_t>(m_snapshots.size()) - static_cast<int64_t>(n))
        )};
        const std::size_t interval_end{m_snapshots.size()};
        remove_snapshots(interval_begin, interval_end);
    }

    const std::vector<Snapshot>& get_snapshots() const
    {
        return m_snapshots;
    }

    std::size_t memsize() const
    {
        std::size_t result{};
        for (const Channel& channel : m_channels | std::views::values) {
            result += channel.memsize();
        }
        for (const Snapshot& snapshot : m_snapshots) {
            for (const SnapshotData& data : snapshot.serialized_data) {
                result += data.data.capacity();
            }
        }
        return result;
    }

private:
    static void assert_consistency(
        const std::map<ChannelId, Channel>& channels,
        const std::vector<Snapshot>& snapshots
    )
    {
        for (std::size_t snapshot_index{}; snapshot_index < snapshots.size(); ++snapshot_index) {
            const Snapshot& snapshot{snapshots[snapshot_index]};
            for (const SnapshotData& data : snapshot.serialized_data) {
                for (const ChannelId& id : data.used_channels) {
                    ASSERT(channels.contains(id));
                    ASSERT(channels.at(id).get_chunk(snapshot_index));
                }
            }
        }
    }

    std::size_t id_to_index(std::size_t id) const
    {
        const auto it{std::ranges::find_if(
            m_snapshots,
            [&](const auto& snapshot) { return snapshot.id == id; }
        )};
        ASSERT(it != m_snapshots.end(), "The id does not exist in the stack anymore!");
        return std::distance(m_snapshots.begin(), it);
    }

    void remove_snapshots(std::size_t interval_begin, std::size_t interval_end)
    {
        ASSERT(interval_begin >= 0 && interval_end <= m_end_snapshot_index);
        for (auto& [_, channel] : m_channels) {
            channel.remove_snapshots(interval_begin, interval_end);
        }

        std::erase_if(m_channels, [](const auto& pair) { return pair.second.empty(); });

        m_snapshots.erase(m_snapshots.begin() + interval_begin, m_snapshots.begin() + interval_end);
        m_end_snapshot_index -= interval_end - interval_begin;

        assert_consistency(m_channels, m_snapshots);
    }

    std::size_t m_end_snapshot_index{};

    std::map<ChannelId, Channel> m_channels;
    std::vector<Snapshot> m_snapshots;
};

Stack::Stack() : m_stack{std::make_unique<SerializedDataStack>()} {}

Stack::Stack(Stack&&) noexcept            = default;
Stack& Stack::operator=(Stack&&) noexcept = default;
Stack::~Stack()                           = default;

constexpr std::size_t snapshot_data_count{6};

// Returns the size of physical memory (RAM) in bytes.
// http://nadeausoftware.com/articles/2012/09/c_c_tip_how_get_physical_memory_size_system
static size_t total_physical_memory()
{
#if defined(_WIN32) && (defined(__CYGWIN__) || defined(__CYGWIN32__))
	// Cygwin under Windows. ------------------------------------
	// New 64-bit MEMORYSTATUSEX isn't available.  Use old 32.bit
	MEMORYSTATUS status;
	status.dwLength = sizeof(status);
	GlobalMemoryStatus( &status );
	return (size_t)status.dwTotalPhys;
#elif defined(_WIN32)
	// Windows. -------------------------------------------------
	// Use new 64-bit MEMORYSTATUSEX, not old 32-bit MEMORYSTATUS
	MEMORYSTATUSEX status;
	status.dwLength = sizeof(status);
	GlobalMemoryStatusEx( &status );
	return (size_t)status.ullTotalPhys;
#elif defined(__unix__) || defined(__unix) || defined(unix) || (defined(__APPLE__) && defined(__MACH__))
	// UNIX variants. -------------------------------------------
	// Prefer sysctl() over sysconf() except sysctl() HW_REALMEM and HW_PHYSMEM

#if defined(CTL_HW) && (defined(HW_MEMSIZE) || defined(HW_PHYSMEM64))
	int mib[2];
	mib[0] = CTL_HW;
#if defined(HW_MEMSIZE)
	mib[1] = HW_MEMSIZE;            // OSX. ---------------------
#elif defined(HW_PHYSMEM64)
	mib[1] = HW_PHYSMEM64;          // NetBSD, OpenBSD. ---------
#endif
	int64_t size = 0;               // 64-bit
	size_t len = sizeof( size );
	if ( sysctl( mib, 2, &size, &len, NULL, 0 ) == 0 )
		return (size_t)size;
	return 0L;			// Failed?

#elif defined(_SC_AIX_REALMEM)
	// AIX. -----------------------------------------------------
	return (size_t)sysconf( _SC_AIX_REALMEM ) * (size_t)1024L;

#elif defined(_SC_PHYS_PAGES) && defined(_SC_PAGESIZE)
	// FreeBSD, Linux, OpenBSD, and Solaris. --------------------
	return (size_t)sysconf( _SC_PHYS_PAGES ) *
		(size_t)sysconf( _SC_PAGESIZE );

#elif defined(_SC_PHYS_PAGES) && defined(_SC_PAGE_SIZE)
	// Legacy. --------------------------------------------------
	return (size_t)sysconf( _SC_PHYS_PAGES ) *
		(size_t)sysconf( _SC_PAGE_SIZE );

#elif defined(CTL_HW) && (defined(HW_PHYSMEM) || defined(HW_REALMEM))
	// DragonFly BSD, FreeBSD, NetBSD, OpenBSD, and OSX. --------
	int mib[2];
	mib[0] = CTL_HW;
#if defined(HW_REALMEM)
	mib[1] = HW_REALMEM;		// FreeBSD. -----------------
#elif defined(HW_PYSMEM)
	mib[1] = HW_PHYSMEM;		// Others. ------------------
#endif
	unsigned int size = 0;		// 32-bit
	size_t len = sizeof( size );
	if ( sysctl( mib, 2, &size, &len, NULL, 0 ) == 0 )
		return (size_t)size;
	return 0L;			// Failed?
#endif // sysctl and sysconf variants

#else
	return 0L;			// Unknown OS.
#endif
}

void Stack::take_snapshot(
    const Domain::Model& model,
    const Biz::Scene::ObjectSelection& object_selection,
    Scene::ToolType selected_tool_gizmo,
    const Domain::Project::ConfigContainerList& config_containers,
    const BedSelectionState& bed_selection_state,
    const ToolsState& tools_state,
    Biz::UndoSnapshotType type
)
{
    model.assert_is_valid();
    const std::vector<Snapshot>& snapshots{m_stack->get_snapshots()};
    if (m_one_past_selected_index != snapshots.size()) {
        m_stack->pop_back_n(snapshots.size() - m_one_past_selected_index);
    }
    ASSERT(m_one_past_selected_index == snapshots.size());

    std::vector<SerializedData> previous_snapshot;
    if (!snapshots.empty()) {
        previous_snapshot = m_stack->get_snapshot_data(snapshots.back());
    }
    std::vector<SerializedData> to_save(snapshot_data_count);
    to_save.at(0) = serialize_model(
        model,
        previous_snapshot.empty() ? SerializedData{} : previous_snapshot.at(0)
    );
    to_save.at(1) = serialize_object_selection(object_selection);
    to_save.at(2) = SerializedData{{}, std::to_string(static_cast<int>(selected_tool_gizmo))};
    to_save.at(3) = serialize_config_container_list(config_containers);
    to_save.at(4) = serialize_bed_selection_state(bed_selection_state, config_containers);
    to_save.at(5) = serialize_tools_state(tools_state);

    m_stack->save_snapshot(to_save, type);
    m_one_past_selected_index++;

    const std::size_t mb{1024 * 1024};
    const std::size_t gb{1024 * mb};
    const std::size_t ridiculous_itertions_count{1000};
    std::size_t count{0};
    std::size_t stack_memsize{m_stack->memsize()};
    const std::size_t stack_memory_limit{std::min(total_physical_memory() / 10, 1 * gb)};
    while (stack_memsize > stack_memory_limit && !m_stack->get_snapshots().empty()) {
        ASSERT(count++ < ridiculous_itertions_count);
        m_stack->pop_front_n(1);
        m_one_past_selected_index = snapshots.size();
        SPDLOG_INFO("Trimming undo stack, original size: {}", stack_memsize);
        stack_memsize = m_stack->memsize();
        SPDLOG_INFO("Trimming undo stack, new size: {}", stack_memsize);
    }

    SPDLOG_TRACE("Undo stack size: {} MB", stack_memsize / static_cast<double>(mb));
    on_change(*this);
}

LoadedSnapshot Stack::load_and_select_snapshot(
    Domain::SelectionId project_id,
    const Snapshot& snapshot,
    Domain::BedContainer& bed_container,
    Biz::Preset::PresetInteractor& preset_interactor
)
{
    const std::vector<SerializedData> to_load{m_stack->get_snapshot_data(snapshot)};
    ASSERT(to_load.size() == snapshot_data_count);
    for (const SerializedData& data : to_load) {
        ASSERT(!data.serialized_data.empty());
    }

    LoadedSnapshot result;
    result.model = load_serialized_model(to_load.at(0));
    result.model.update_links_bottom_up_recursive();
    result.model.assert_is_valid();
    result.object_selection = load_serialized_object_selection(to_load.at(1));
    result.selected_tool_gizmo =
        static_cast<Scene::ToolType>(std::stoi(to_load.at(2).serialized_data));
    result.config_containers = load_serialized_config_container_list(
        project_id,
        to_load.at(3),
        bed_container,
        preset_interactor
    );
    result.bed_selection_state =
        load_serialized_bed_selection_state(to_load.at(4), result.config_containers);
    result.tools_state         = load_serialized_tools_state(to_load.at(5));

    const std::vector<Snapshot>& snapshots{m_stack->get_snapshots()};
    const auto it{std::ranges::find_if(
        snapshots,
        [&](const Snapshot& stack_snapshot) { return snapshot.id == stack_snapshot.id; }
    )};
    ASSERT(it != snapshots.end());
    m_one_past_selected_index = std::distance(snapshots.begin(), it) + 1;
    on_change(*this);
    return result;
}

const std::vector<Snapshot>& Stack::get_snapshots() const
{
    return m_stack->get_snapshots();
}

std::optional<std::size_t> Stack::get_selected_index() const
{
    if (m_one_past_selected_index == 0) {
        return std::nullopt;
    }
    return m_one_past_selected_index - 1;
};
} // namespace Slic3r::App::Undo
