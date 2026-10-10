#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <list>
#include <memory>
#include <random>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

/*
    B+ Tree Implementation Assignment
    ----------------------------------
    Modes:
      1. memory  : all B+ Tree pages are stored in RAM
      2. disk    : disk pages + a one-page cache (disk-only comparison)
      3. hybrid  : disk pages + a larger in-memory page cache

    The same B+ Tree algorithms are used in each mode. Only the page-storage
    layer changes, so the benchmark compares storage strategies fairly.

    Assumptions:
      - Keys are unique alphanumeric strings, up to 31 characters.
      - Payloads are strings, up to 127 characters (the supplied sample fits).
      - A node contains at most 31 keys; a split creates two smaller nodes.
      - Disk pages are fixed-size binary records equal to sizeof(Node).
      - The disk-only mode uses a one-page cache; hybrid uses 256 pages.
      - Search timing is measured over up to 10,000 existing keys.
*/

namespace
{
    constexpr int MAX_KEYS = 31;
    constexpr int KEY_BYTES = 32;
    constexpr int PAYLOAD_BYTES = 128;
    constexpr int MAX_CHILDREN = MAX_KEYS + 1;

    struct Entry
    {
        char key[KEY_BYTES]{};
        char payload[PAYLOAD_BYTES]{};
    };

    // Fixed-size node makes each disk page easy to seek to by page number.
    struct Node
    {
        bool leaf = true;
        int count = 0;
        std::uint64_t nextLeaf = 0;                 // Linked leaves make range scans possible.
        Entry entries[MAX_KEYS + 1]{};              // One extra slot is used temporarily on split.
        std::uint64_t children[MAX_CHILDREN + 1]{}; // One extra slot for split handling.
    };

    std::string safeString(const char *value, std::size_t size)
    {
        return std::string(value, std::find(value, value + size, '\0'));
    }

    void copyText(char *destination, std::size_t capacity, const std::string &source)
    {
        std::memset(destination, 0, capacity);
        std::memcpy(destination, source.data(), std::min(capacity - 1, source.size()));
    }

    struct IOStats
    {
        std::uint64_t reads = 0;
        std::uint64_t writes = 0;
        std::uint64_t cacheHits = 0;
    };

    // Storage interface used by the tree. Page IDs start at 1; 0 means "no page".
    class PageStore
    {
    public:
        virtual ~PageStore() = default;
        virtual std::uint64_t create(const Node &node) = 0;
        virtual Node load(std::uint64_t id) = 0;
        virtual void save(std::uint64_t id, const Node &node) = 0;
        virtual void flush() = 0;
        virtual std::uint64_t pageCount() const = 0;
        virtual std::uint64_t bytesUsed() const = 0;
        virtual IOStats stats() const = 0;
    };

    // SECTION 1: MEMORY-ONLY PAGE STORAGE -----------------------------------------
    class MemoryStore : public PageStore
    {
        std::unordered_map<std::uint64_t, Node> pages;
        std::uint64_t nextId = 1;
        IOStats io{};

    public:
        std::uint64_t create(const Node &node) override
        {
            const auto id = nextId++;
            pages[id] = node;
            return id;
        }
        Node load(std::uint64_t id) override
        {
            auto it = pages.find(id);
            if (it == pages.end())
                throw std::runtime_error("Invalid in-memory page ID");
            return it->second;
        }
        void save(std::uint64_t id, const Node &node) override { pages.at(id) = node; }
        void flush() override {}
        std::uint64_t pageCount() const override { return pages.size(); }
        std::uint64_t bytesUsed() const override
        {
            // Approximation: node bytes plus a small per-node hash-table overhead.
            return pages.size() * (sizeof(Node) + 32);
        }
        IOStats stats() const override { return io; }
    };

    // SECTION 2 & 3: DISK PAGE STORAGE WITH A CONFIGURABLE LRU CACHE ---------------
    class DiskStore : public PageStore
    {
        struct CacheEntry
        {
            Node node{};
            bool dirty = false;
            std::list<std::uint64_t>::iterator position;
        };
        std::fstream file;
        std::string filename;
        std::size_t capacity;
        std::uint64_t nextId = 1;
        std::unordered_map<std::uint64_t, CacheEntry> cache;
        std::list<std::uint64_t> lru; // Most recently used page is at the front.
        IOStats io{};

        void writePage(std::uint64_t id, const Node &node)
        {
            file.clear();
            file.seekp(static_cast<std::streamoff>((id - 1) * sizeof(Node)), std::ios::beg);
            file.write(reinterpret_cast<const char *>(&node), sizeof(Node));
            if (!file)
                throw std::runtime_error("Failed to write disk page");
            ++io.writes;
        }
        Node readPage(std::uint64_t id)
        {
            Node node{};
            file.clear();
            file.seekg(static_cast<std::streamoff>((id - 1) * sizeof(Node)), std::ios::beg);
            file.read(reinterpret_cast<char *>(&node), sizeof(Node));
            if (!file)
                throw std::runtime_error("Failed to read disk page " + std::to_string(id));
            ++io.reads;
            return node;
        }
        void touch(typename std::unordered_map<std::uint64_t, CacheEntry>::iterator it)
        {
            lru.erase(it->second.position);
            lru.push_front(it->first);
            it->second.position = lru.begin();
        }
        void evictIfNeeded()
        {
            while (cache.size() > capacity)
            {
                const auto id = lru.back();
                auto it = cache.find(id);
                if (it->second.dirty)
                    writePage(id, it->second.node);
                lru.pop_back();
                cache.erase(it);
            }
        }

    public:
        DiskStore(const std::string &path, std::size_t cachePages) : filename(path), capacity(cachePages)
        {
            // Each benchmark starts with a fresh file, so old pages cannot leak in.
            file.open(filename, std::ios::in | std::ios::out | std::ios::binary | std::ios::trunc);
            if (!file)
                throw std::runtime_error("Cannot create disk file: " + filename);
        }
        ~DiskStore() override
        {
            try
            {
                flush();
            }
            catch (...)
            {
            }
            if (file.is_open())
                file.close();
        }
        std::uint64_t create(const Node &node) override
        {
            const auto id = nextId++;
            cache[id] = CacheEntry{node, true, {}};
            lru.push_front(id);
            cache[id].position = lru.begin();
            evictIfNeeded();
            return id;
        }
        Node load(std::uint64_t id) override
        {
            auto it = cache.find(id);
            if (it != cache.end())
            {
                ++io.cacheHits;
                touch(it);
                return it->second.node;
            }
            Node node = readPage(id);
            lru.push_front(id);
            cache.emplace(id, CacheEntry{node, false, lru.begin()});
            evictIfNeeded();
            return node;
        }
        void save(std::uint64_t id, const Node &node) override
        {
            auto it = cache.find(id);
            if (it == cache.end())
            {
                lru.push_front(id);
                cache.emplace(id, CacheEntry{node, true, lru.begin()});
            }
            else
            {
                it->second.node = node;
                it->second.dirty = true;
                touch(it);
            }
            evictIfNeeded();
        }
        void flush() override
        {
            for (auto &item : cache)
            {
                if (item.second.dirty)
                {
                    writePage(item.first, item.second.node);
                    item.second.dirty = false;
                }
            }
            file.flush();
        }
        std::uint64_t pageCount() const override { return nextId - 1; }
        std::uint64_t bytesUsed() const override
        {
            std::error_code ec;
            auto size = std::filesystem::file_size(filename, ec);
            return ec ? pageCount() * sizeof(Node) : size;
        }
        IOStats stats() const override { return io; }
    };

    // SECTION 4: B+ TREE OPERATIONS (INSERT, SPLIT, SEARCH) ------------------------
    class BPlusTree
    {
        PageStore &store;
        std::uint64_t root = 0;

        struct SplitResult
        {
            bool split = false;
            std::string separator;
            std::uint64_t rightPage = 0;
        };

        static std::string keyAt(const Node &node, int i)
        {
            return safeString(node.entries[i].key, KEY_BYTES);
        }
        static std::string payloadAt(const Node &node, int i)
        {
            return safeString(node.entries[i].payload, PAYLOAD_BYTES);
        }
        static void setEntry(Entry &entry, const std::string &key, const std::string &payload)
        {
            copyText(entry.key, KEY_BYTES, key);
            copyText(entry.payload, PAYLOAD_BYTES, payload);
        }

        SplitResult insertRecursive(std::uint64_t pageId, const std::string &key,
                                    const std::string &payload)
        {
            Node node = store.load(pageId);
            int i = 0;
            while (i < node.count && keyAt(node, i) < key)
                ++i;

            if (node.leaf)
            {
                if (i < node.count && keyAt(node, i) == key)
                    throw std::runtime_error("Duplicate key found: " + key);
                for (int j = node.count; j > i; --j)
                    node.entries[j] = node.entries[j - 1];
                setEntry(node.entries[i], key, payload);
                ++node.count;
                if (node.count <= MAX_KEYS)
                {
                    store.save(pageId, node);
                    return {};
                }
                // Split overflowing leaf roughly in half and maintain leaf links.
                const int leftCount = node.count / 2;
                Node right{};
                right.leaf = true;
                right.count = node.count - leftCount;
                for (int j = 0; j < right.count; ++j)
                    right.entries[j] = node.entries[leftCount + j];
                right.nextLeaf = node.nextLeaf;
                node.count = leftCount;
                const auto rightId = store.create(right);
                node.nextLeaf = rightId;
                store.save(pageId, node);
                return {true, keyAt(right, 0), rightId};
            }

            // Internal nodes store separator keys and child page IDs.
            while (i < node.count && key >= keyAt(node, i))
                ++i;
            SplitResult childSplit = insertRecursive(node.children[i], key, payload);
            if (!childSplit.split)
                return {};
            for (int j = node.count; j > i; --j)
                node.entries[j] = node.entries[j - 1];
            for (int j = node.count + 1; j > i + 1; --j)
                node.children[j] = node.children[j - 1];
            copyText(node.entries[i].key, KEY_BYTES, childSplit.separator);
            node.entries[i].payload[0] = '\0';
            node.children[i + 1] = childSplit.rightPage;
            ++node.count;
            if (node.count <= MAX_KEYS)
            {
                store.save(pageId, node);
                return {};
            }

            const int mid = node.count / 2;
            const std::string promoted = keyAt(node, mid);
            Node right{};
            right.leaf = false;
            right.count = node.count - mid - 1;
            for (int j = 0; j < right.count; ++j)
                right.entries[j] = node.entries[mid + 1 + j];
            for (int j = 0; j <= right.count; ++j)
                right.children[j] = node.children[mid + 1 + j];
            node.count = mid;
            const auto rightId = store.create(right);
            store.save(pageId, node);
            return {true, promoted, rightId};
        }

    public:
        explicit BPlusTree(PageStore &pageStore) : store(pageStore)
        {
            Node firstLeaf{};
            firstLeaf.leaf = true;
            root = store.create(firstLeaf);
        }
        void insert(const std::string &key, const std::string &payload)
        {
            SplitResult result = insertRecursive(root, key, payload);
            if (result.split)
            {
                Node newRoot{};
                newRoot.leaf = false;
                newRoot.count = 1;
                copyText(newRoot.entries[0].key, KEY_BYTES, result.separator);
                newRoot.children[0] = root;
                newRoot.children[1] = result.rightPage;
                root = store.create(newRoot);
            }
        }
        // Return the actual tree height by following the left-most branch.
        int height()
        {
            int levels = 0;
            std::uint64_t pageId = root;
            while (pageId != 0)
            {
                Node node = store.load(pageId);
                ++levels;
                if (node.leaf)
                    break;
                pageId = node.children[0];
            }
            return levels;
        }

        bool search(const std::string &key, std::string *payloadOut = nullptr)
        {
            std::uint64_t pageId = root;
            while (true)
            {
                Node node = store.load(pageId);
                int i = 0;
                if (node.leaf)
                {
                    while (i < node.count && keyAt(node, i) < key)
                        ++i;
                    if (i < node.count && keyAt(node, i) == key)
                    {
                        if (payloadOut)
                            *payloadOut = payloadAt(node, i);
                        return true;
                    }
                    return false;
                }
                while (i < node.count && key >= keyAt(node, i))
                    ++i;
                pageId = node.children[i];
            }
        }
    };

    struct Record
    {
        std::string key, payload;
    };

    // SECTION 5: CSV READING -------------------------------------------------------
    std::vector<Record> readCSV(const std::string &path, std::size_t limit)
    {
        std::ifstream input(path);
        if (!input)
            throw std::runtime_error("Cannot open CSV file: " + path);
        std::vector<Record> records;
        if (limit > 0)
            records.reserve(limit);
        std::string line;
        std::getline(input, line); // Skip header: key,payload
        std::size_t lineNumber = 1;
        while (std::getline(input, line))
        {
            ++lineNumber;
            if (line.empty())
                continue;
            const auto comma = line.find(',');
            if (comma == std::string::npos)
            {
                std::cerr << "Skipping malformed CSV line " << lineNumber << '\n';
                continue;
            }
            Record record{line.substr(0, comma), line.substr(comma + 1)};
            if (record.key.size() >= KEY_BYTES || record.payload.size() >= PAYLOAD_BYTES)
            {
                throw std::runtime_error("Key/payload too long at CSV line " + std::to_string(lineNumber) +
                                         ". Increase KEY_BYTES/PAYLOAD_BYTES in the source.");
            }
            records.push_back(std::move(record));
            if (limit > 0 && records.size() >= limit)
                break;
        }
        return records;
    }

    // SECTION 6: BENCHMARK AND RESULT REPORT ---------------------------------------
    struct Result
    {
        std::string mode;
        double insertSeconds = 0, searchMicros = 0;
        std::uint64_t pages = 0, bytes = 0;
        int height = 0; // Height estimated by following the left-most path.
        IOStats io{};
    };

    Result runBenchmark(const std::string &mode, const std::vector<Record> &records,
                        const std::string &diskFile, std::size_t cachePages)
    {
        std::unique_ptr<PageStore> store;
        if (mode == "memory")
            store = std::make_unique<MemoryStore>();
        else
            store = std::make_unique<DiskStore>(diskFile, cachePages);

        BPlusTree tree(*store);
        const auto insertStart = std::chrono::steady_clock::now();
        for (std::size_t i = 0; i < records.size(); ++i)
        {
            tree.insert(records[i].key, records[i].payload);
            if ((i + 1) % 1000000 == 0)
                std::cout << "  inserted " << (i + 1) << " records\r" << std::flush;
        }
        const auto insertEnd = std::chrono::steady_clock::now();
        store->flush();
        if (!records.empty())
            std::cout << "  inserted " << records.size() << " records\n";

        // Use a deterministic sample of existing keys, so each mode checks the same count.
        const std::size_t sampleCount = std::min<std::size_t>(records.size(), 10000);
        std::vector<std::size_t> indices;
        indices.reserve(sampleCount);
        std::mt19937 rng(42);
        for (std::size_t i = 0; i < sampleCount; ++i)
            indices.push_back(i);
        std::shuffle(indices.begin(), indices.end(), rng);
        const auto searchStart = std::chrono::steady_clock::now();
        for (auto index : indices)
        {
            std::string foundPayload;
            if (!tree.search(records[index].key, &foundPayload))
                throw std::runtime_error("Search failed for inserted key: " + records[index].key);
            if (foundPayload != records[index].payload)
                throw std::runtime_error("Payload mismatch for key: " + records[index].key);
        }
        const auto searchEnd = std::chrono::steady_clock::now();

        Result result;
        result.mode = mode;
        result.insertSeconds = std::chrono::duration<double>(insertEnd - insertStart).count();
        result.searchMicros = sampleCount ? std::chrono::duration<double, std::micro>(searchEnd - searchStart).count() / sampleCount : 0;
        result.pages = store->pageCount();
        result.bytes = store->bytesUsed();
        result.height = tree.height();
        result.io = store->stats();
        return result;
    }

    void printResult(const Result &r)
    {
        std::cout << "\n=== " << r.mode << " B+ Tree ===\n"
                  << "Insertion time       : " << std::fixed << std::setprecision(3) << r.insertSeconds << " s\n"
                  << "Average search time  : " << std::fixed << std::setprecision(3) << r.searchMicros << " us/key\n"
                  << "Tree pages/nodes     : " << r.pages << "\n"
                  << "Storage size         : " << r.bytes << " bytes (" << (r.bytes / (1024.0 * 1024.0)) << " MiB)\n"
                  << "Tree height: " << r.height << "\n"
                  << "Disk page reads      : " << r.io.reads << "\n"
                  << "Disk page writes     : " << r.io.writes << "\n"
                  << "Cache hits           : " << r.io.cacheHits << "\n";
    }

} // namespace

int main(int argc, char *argv[])
{
    try
    {
        // Usage: bplus_tree.exe <csv_path> [limit]
        // Use limit=10000 for a quick correctness test; omit it for all records.
        if (argc < 2)
        {
            std::cout << "Usage: " << argv[0] << " <csv_path> [limit]\n"
                      << "Example: " << argv[0] << " bplus_tree_data_10m.csv 10000\n"
                      << "Use limit 0 (or omit limit) to load all records.\n";
            return 0;
        }
        const std::string csvPath = argv[1];
        std::size_t limit = 0;
        if (argc >= 3)
            limit = static_cast<std::size_t>(std::stoull(argv[2]));
        std::cout << "Reading CSV: " << csvPath << "\n";
        std::vector<Record> records = readCSV(csvPath, limit);
        if (records.empty())
            throw std::runtime_error("No data records found in CSV.");
        std::cout << "Loaded " << records.size() << " records. Node/page size: " << sizeof(Node)
                  << " bytes; maximum keys per node: " << MAX_KEYS << "\n";

        // SECTION 7: RUN ALL THREE STORAGE CONFIGURATIONS ----------------------
        std::vector<Result> results;
        results.push_back(runBenchmark("memory", records, "", 0));
        results.push_back(runBenchmark("disk", records, "bpt_disk_only.pages", 1));
        results.push_back(runBenchmark("hybrid", records, "bpt_hybrid.pages", 256));

        std::ofstream report("benchmark_results.csv");
        report << "mode,records,insertion_seconds,avg_search_microseconds,pages,storage_bytes,estimated_height,disk_reads,disk_writes,cache_hits\n";
        for (const auto &r : results)
        {
            printResult(r);
            report << r.mode << ',' << records.size() << ',' << r.insertSeconds << ',' << r.searchMicros
                   << ',' << r.pages << ',' << r.bytes << ',' << r.height << ',' << r.io.reads
                   << ',' << r.io.writes << ',' << r.io.cacheHits << '\n';
        }
        std::cout << "\nSaved results to benchmark_results.csv\n"
                  << "Disk page files: bpt_disk_only.pages and bpt_hybrid.pages\n";
        return 0;
    }
    catch (const std::exception &ex)
    {
        std::cerr << "ERROR: " << ex.what() << '\n';
        return 1;
    }
}
