#include "small_map.hpp"
#include <fstream>
#include <mutex>

namespace {
    struct CsvWriter {
    public:
        inline CsvWriter(const std::string& outPath, const std::vector<std::string>& fields)
            : cols(fields), ofs(outPath) {

			std::lock_guard<std::mutex> lck(mtx);
			ncols = fields.size();

            for (auto& field : fields) {
                if (field != *fields.begin()) ofs << ",";
				ofs << field;
            }
			ofs << "\n";
        }

        inline void set(const std::string& key, std::string&& val) {
            row.insert(key, std::move(val));
        }

        inline void finishRow() {
            // for (auto& kv : row) {
			for (size_t i=0; i<ncols; i++) {
				if (i > 0) ofs << ",";
				const std::string& k = cols[i];
				auto it = row.find(k);
				if (it != row.end()) {
					ofs << it->second;
				}
            }
			ofs << "\n";
			row = decltype(row){};
        }

		inline std::lock_guard<std::mutex> lock() { return std::lock_guard<std::mutex>(mtx); }

    private:
		std::vector<std::string> cols;
        babus::SmallMap<std::string, std::string> row;
		size_t ncols = 0;
        std::ofstream ofs;
		std::mutex mtx;
    };
}
