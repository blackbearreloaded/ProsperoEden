// SPDX-License-Identifier: GPL-3.0-or-later
// Reading a game's shader cache file (in place of Eden's LoadPipelines; headless/CMakeLists.txt).
// The file grows by one entry per new pipeline while a game runs, so a crash or a console that
// goes down can leave it with a tail that is cut off, zeroed or random. Eden removed a file that
// ended early and trusted everything else: an entry announcing no shaders, or sizes far beyond
// the file, made every later start of that game fail the same way. Here an entry is checked
// before it is read, any failure ends the reading, and the file is cut back to its last whole
// entry: those stay usable and the game builds the rest again as it runs.
namespace {
struct DamagedCache {};

// An entry's environment begins with five sizes; none can exceed what is left of the file.
void CheckEnvironment(std::ifstream& file, std::streamoff end) {
    const std::streamoff start = file.tellg();
    std::array<u64, 5> sizes{};
    file.read(reinterpret_cast<char*>(sizes.data()), sizeof(sizes));
    const u64 left = static_cast<u64>(end - start);
    for (const u64 size : sizes) {
        if (size > left) throw DamagedCache{};
    }
    file.seekg(start);
}
} // namespace

void LoadPipelines(
    std::stop_token stop_loading, const std::filesystem::path& filename, u32 expected_cache_version,
    Common::UniqueFunction<void, std::ifstream&, FileEnvironment> load_compute,
    Common::UniqueFunction<void, std::ifstream&, std::vector<FileEnvironment>> load_graphics) {
    // A pipeline has at most this many shaders (two vertex programs, tessellation control and
    // evaluation, geometry, fragment).
    constexpr u32 kMostShaders = 6;
    std::streamoff good = -1;  // the end of the last whole entry; -1: not even the header
    try {
        std::ifstream file(filename, std::ios::binary | std::ios::ate);
        if (!file.is_open()) {
            return;
        }
        file.exceptions(std::ifstream::failbit);
        const std::streamoff end{file.tellg()};
        file.seekg(0, std::ios::beg);

        std::array<char, 8> magic_number;
        u32 cache_version;
        file.read(magic_number.data(), magic_number.size())
            .read(reinterpret_cast<char*>(&cache_version), sizeof(cache_version));
        if (magic_number != MAGIC_NUMBER || cache_version != expected_cache_version) {
            file.close();
            if (Common::FS::RemoveFile(filename)) {
                if (magic_number != MAGIC_NUMBER) {
                    LOG_ERROR(Common_Filesystem, "Invalid pipeline cache file");
                }
                if (cache_version != expected_cache_version) {
                    LOG_INFO(Common_Filesystem, "Deleting old pipeline cache");
                }
            } else {
                LOG_ERROR(Common_Filesystem,
                          "Invalid pipeline cache file and failed to delete it in \"{}\"",
                          Common::FS::PathToUTF8String(filename));
            }
            return;
        }
        good = file.tellg();
        while (good != end) {
            if (stop_loading.stop_requested()) {
                return;
            }
            u32 num_envs{};
            file.read(reinterpret_cast<char*>(&num_envs), sizeof(num_envs));
            if (num_envs == 0 || num_envs > kMostShaders) {
                throw DamagedCache{};
            }
            std::vector<FileEnvironment> envs(num_envs);
            for (FileEnvironment& env : envs) {
                CheckEnvironment(file, end);
                env.Deserialize(file);
            }
            if (envs.front().ShaderStage() == Shader::Stage::Compute) {
                load_compute(file, std::move(envs.front()));
            } else {
                load_graphics(file, std::move(envs));
            }
            good = file.tellg();
        }
        return;
    } catch (const std::ios_base::failure&) {
    } catch (const DamagedCache&) {
    } catch (const std::bad_alloc&) {
    } catch (const std::length_error&) {
    }
    std::error_code error;
    if (good > 0) {
        std::filesystem::resize_file(filename, static_cast<std::uintmax_t>(good), error);
    }
    if (good <= 0 || error) {
        (void)Common::FS::RemoveFile(filename);
    }
    std::fprintf(stderr, "[ProsperoEden] shader cache: %s is damaged after %lld bytes; %s\n",
                 Common::FS::PathToUTF8String(filename).c_str(), static_cast<long long>(good < 0 ? 0 : good),
                 good > 0 && !error ? "the entries before that are kept" : "the file is removed");
}
