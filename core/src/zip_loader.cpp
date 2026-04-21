#include "zip_loader.h"

#include "miniz.h"

namespace ss {

std::vector<ZipEntry> extract_zip(const char* zip_path) {
    std::vector<ZipEntry> out;
    if (!zip_path) return out;

    mz_zip_archive zip{};
    if (!mz_zip_reader_init_file(&zip, zip_path, 0)) return out;

    mz_uint n = mz_zip_reader_get_num_files(&zip);
    out.reserve(n);

    for (mz_uint i = 0; i < n; ++i) {
        if (mz_zip_reader_is_file_a_directory(&zip, i)) continue;

        mz_zip_archive_file_stat st;
        if (!mz_zip_reader_file_stat(&zip, i, &st)) continue;

        ZipEntry e;
        e.name.assign(st.m_filename);
        e.data.resize(static_cast<size_t>(st.m_uncomp_size));
        if (!mz_zip_reader_extract_to_mem(&zip, i, e.data.data(), e.data.size(), 0)) {
            // skip this entry; keep going
            continue;
        }
        out.push_back(std::move(e));
    }

    mz_zip_reader_end(&zip);
    return out;
}

} // namespace ss
