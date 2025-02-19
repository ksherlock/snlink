#include "omf.h"

#include <vector>
#include <string>
#include <algorithm>
#include <array>

#include <unistd.h>
#include <fcntl.h>
#include <err.h>
#include <sysexits.h>
#include <assert.h>

// #include "optional.h"
#include <optional>

#ifndef O_BINARY
#define O_BINARY 0
#endif

enum class endian {
#ifdef _WIN32
    little = 0,
    big    = 1,
    native = little
#else
    little = __ORDER_LITTLE_ENDIAN__,
    big    = __ORDER_BIG_ENDIAN__,
    native = __BYTE_ORDER__
#endif
};

#pragma pack(push, 1)
struct omf_header {
	uint32_t bytecount = 0;
	uint32_t reserved_space = 0;
	uint32_t length = 0;
	uint8_t unused1 = 0;
	uint8_t lablen = 0;
	uint8_t numlen = 4;
	uint8_t version = 2;
	uint32_t banksize = 0;
	uint16_t kind = 0;
	uint16_t unused2 = 0;
	uint32_t org = 0;
	uint32_t alignment = 0;
	uint8_t numsex = 0;
	uint8_t unused3 = 0;
	uint16_t segnum = 0;
	uint32_t entry = 0;
	uint16_t dispname = 0;
	uint16_t dispdata = 0;
};

struct omf_express_header {
	uint32_t lconst_mark;
	uint32_t lconst_size;
	uint32_t reloc_mark;
	uint32_t reloc_size;
	uint8_t unused1 = 0;
	uint8_t lablen = 0;
	uint8_t numlen = 4;
	uint8_t version = 2;
	uint32_t banksize = 0;
	uint16_t kind = 0;
	uint16_t unused2 = 0;
	uint32_t org = 0;
	uint32_t alignment = 0;
	uint8_t numsex = 0;
	uint8_t unused3 = 0;
	uint16_t segnum = 0;
	uint32_t entry = 0;
	uint16_t dispname = 0;
	uint16_t dispdata = 0;
};

#pragma pack(pop)

static_assert(sizeof(omf_header) == 44, "OMF Header not packed");
static_assert(sizeof(omf_express_header) == 48, "OMF Express Header not packed");


static void swap(uint8_t &x) {}
static void swap(uint16_t &x) { 
	#if defined(__GNUC__)
	x = __builtin_bswap16(x);
	#else
	x = (x >> 8) | (x << 8);
	#endif
}
static void swap(uint32_t &x) {
	#if defined(__GNUC__)
	x = __builtin_bswap32(x);
	#else
	x = ((x & 0xff000000) >> 24) |
		((x & 0x00ff0000) >> 8) |
		((x & 0x0000ff00) << 8) |
		((x & 0x000000ff) << 24);
	#endif
}


static void to_little(struct omf_header &h) {
	if (endian::native != endian::little) {
		swap(h.bytecount);
		swap(h.reserved_space);
		swap(h.length);
		swap(h.unused1);
		swap(h.lablen);
		swap(h.numlen);
		swap(h.version);
		swap(h.banksize);
		swap(h.kind);
		swap(h.unused2);
		swap(h.org);
		swap(h.alignment);
		swap(h.numsex);
		swap(h.unused3);
		swap(h.segnum);
		swap(h.entry);
		swap(h.dispname);
		swap(h.dispdata);
	}
}

[[maybe_unused]] static void to_little(struct omf_express_header &h) {
	if (endian::native != endian::little) {
		swap(h.lconst_mark);
		swap(h.lconst_size);
		swap(h.reloc_mark);
		swap(h.reloc_size);
		swap(h.unused1);
		swap(h.lablen);
		swap(h.numlen);
		swap(h.version);
		swap(h.banksize);
		swap(h.kind);
		swap(h.unused2);
		swap(h.org);
		swap(h.alignment);
		swap(h.numsex);
		swap(h.unused3);
		swap(h.segnum);
		swap(h.entry);
		swap(h.dispname);
		swap(h.dispdata);
	}
}


static void to_v1(struct omf_header &h) {

	// KIND op value used as-is, no translation.

	h.version = 1;
	// byte count -> block count
	h.bytecount = (h.bytecount + 511) >> 9;
	h.unused1 = h.kind;
	h.kind = 0;
}


void push(std::vector<uint8_t> &v, uint8_t x) {
	v.push_back(x);
}

void push(std::vector<uint8_t> &v, uint16_t x) {
	v.push_back(x & 0xff);
	x >>= 8;
	v.push_back(x & 0xff);
}

void push(std::vector<uint8_t> &v, uint32_t x) {
	v.push_back(x & 0xff);
	x >>= 8;
	v.push_back(x & 0xff);
	x >>= 8;
	v.push_back(x & 0xff);
	x >>= 8;
	v.push_back(x & 0xff);
}

void push(std::vector<uint8_t> &v, const std::string &s) {
	uint8_t count = std::min((int)s.size(), 255);
	push(v, count);
	v.insert(v.end(), s.begin(), s.begin() + count);
}

void push(std::vector<uint8_t> &v, const std::string &s, size_t count) {
	std::string tmp(s, 0, count);
	tmp.resize(count, ' ');
	v.insert(v.end(), tmp.begin(), tmp.end());
}

class super_helper {

	std::vector<uint8_t> _data;
	uint32_t _page = 0;
	int _count = 0;

public:

	super_helper() = default;


	void append(uint32_t pc) {
		unsigned offset = pc & 0xff;
		unsigned page = pc >> 8;
		assert(page >= _page);

		if (page != _page) {
			unsigned skip = page - _page;
			if (_count) --skip;
			if (skip) {
				
				while (skip >= 0x80) {
					_data.push_back(0xff);
					skip -= 0x7f;
				}
				if (skip)
					_data.push_back(0x80 | skip);
			}
			_page = page;
			_count = 0;
		}

		if (!_count) {
			_data.push_back(0); // count-1 place holder.
		} else {
			_data[_data.size() - _count - 1] = _count; // count is count - 1 at this point,
		}

		_data.push_back(offset);
		++_count;
	}

	void reset() {
		_data.clear();
		_page = 0;
		_count = 0;
	}

	const std::vector<uint8_t> &data() {
		return _data;
	}

};


enum {
	SUPER_RELOC2,
	SUPER_RELOC3,
	SUPER_INTERSEG1,
	SUPER_INTERSEG2,
	SUPER_INTERSEG3,
	SUPER_INTERSEG4,
	SUPER_INTERSEG5,
	SUPER_INTERSEG6,
	SUPER_INTERSEG7,
	SUPER_INTERSEG8,
	SUPER_INTERSEG9,
	SUPER_INTERSEG10,
	SUPER_INTERSEG11,
	SUPER_INTERSEG12,
	SUPER_INTERSEG13,
	SUPER_INTERSEG14,
	SUPER_INTERSEG15,
	SUPER_INTERSEG16,
	SUPER_INTERSEG17,
	SUPER_INTERSEG18,
	SUPER_INTERSEG19,
	SUPER_INTERSEG20,
	SUPER_INTERSEG21,
	SUPER_INTERSEG22,
	SUPER_INTERSEG23,
	SUPER_INTERSEG24,
	SUPER_INTERSEG25,
	SUPER_INTERSEG26,
	SUPER_INTERSEG27,
	SUPER_INTERSEG28,
	SUPER_INTERSEG29,
	SUPER_INTERSEG30,
	SUPER_INTERSEG31,
	SUPER_INTERSEG32,
	SUPER_INTERSEG33,
	SUPER_INTERSEG34,
	SUPER_INTERSEG35,
	SUPER_INTERSEG36,
};

uint32_t add_relocs(std::vector<uint8_t> &data, size_t data_offset, omf::segment &seg, bool compress, bool super) {

	std::array< std::optional<super_helper>, 38 > ss;


	uint32_t reloc_size = 0;

	for (auto &r : seg.relocs) {

		if (compress && r.can_compress()) {

			if (super) {
				if (r.shift == 0 && r.size == 2) {
					constexpr int n = SUPER_RELOC2;

					auto &sr = ss[n];
					if (!sr) sr.emplace();
					sr->append(r.offset);

					uint32_t value = r.value;
					for (int i = 0; i < 2; ++i, value >>= 8)
						data[data_offset + r.offset + i] = value; 
					continue;
				}

				// sreloc 3 is for 3 bytes.  however 4 bytes is also ok since 
				// it's 24-bit address space.
				if (r.shift == 0 && (r.size == 2 || r.size == 3))
				{
					constexpr int n = SUPER_RELOC3;

					auto &sr = ss[n];
					if (!sr) sr.emplace();
					sr->append(r.offset);

					uint32_t value = r.value;
					for (int i = 0; i < 3; ++i, value >>= 8)
						data[data_offset + r.offset + i] = value; 
					continue;	
				}

				// if size == 2 && shift == -16, -> SUPER INTERSEG 
				// but should we interseg if it's the same segment???
				if (seg.segnum <= 12 && r.shift == 0xf0 && r.size == 2) {

					int n = SUPER_INTERSEG24 + seg.segnum;
					auto &sr = ss[n];
					if (!sr) sr.emplace();
					sr->append(r.offset);

					uint32_t value = r.value;
					for (int i = 0; i < 2; ++i, value >>= 8)
						data[data_offset + r.offset + i] = value; 
					continue;
				}
			}

			push(data, (uint8_t)omf::cRELOC);
			push(data, (uint8_t)r.size);
			push(data, (uint8_t)r.shift);
			push(data, (uint16_t)r.offset);
			push(data, (uint16_t)r.value);
			reloc_size += 7;
		} else {
			push(data, (uint8_t)omf::RELOC);
			push(data, (uint8_t)r.size);
			push(data, (uint8_t)r.shift);
			push(data, (uint32_t)r.offset);
			push(data, (uint32_t)r.value);
			reloc_size += 11;
		}
	}

	for (const auto &r : seg.intersegs) {
		if (compress && r.can_compress()) {

			if (super) {

				if (r.shift == 0 && r.size == 3) {
					constexpr int n = SUPER_INTERSEG1;
					auto &sr = ss[n];
					if (!sr) sr.emplace();
					sr->append(r.offset);

					uint32_t value = r.segment_offset;

					data[data_offset + r.offset + 0] = value; value >>= 8;
					data[data_offset + r.offset + 1] = value; value >>= 8;
					data[data_offset + r.offset + 2] = r.segment;
					continue;
				}


				if (r.shift == 0 && r.size == 2 && r.segment <= 12) {

					int n = SUPER_INTERSEG12 + r.segment;
					auto &sr = ss[n];
					if (!sr) sr.emplace();
					sr->append(r.offset);

					uint32_t value = r.segment_offset;
					for (int i = 0; i < 2; ++i, value >>= 8)
						data[data_offset + r.offset + i] = value; 
					continue;
				}

				if (r.shift == 0xf0 && r.size == 2 && r.segment <= 12) {

					int n = SUPER_INTERSEG24 + r.segment;
					auto &sr = ss[n];
					if (!sr) sr.emplace();
					sr->append(r.offset);

					uint32_t value = r.segment_offset;
					for (int i = 0; i < 2; ++i, value >>= 8)
						data[data_offset + r.offset + i] = value; 
					continue;
				}
			}


			push(data, (uint8_t)omf::cINTERSEG);
			push(data, (uint8_t)r.size);
			push(data, (uint8_t)r.shift);
			push(data, (uint16_t)r.offset);
			push(data, (uint8_t)r.segment);
			push(data, (uint16_t)r.segment_offset);
			reloc_size += 8;
		} else {
			push(data, (uint8_t)omf::INTERSEG);
			push(data, (uint8_t)r.size);
			push(data, (uint8_t)r.shift);
			push(data, (uint32_t)r.offset);
			push(data, (uint16_t)r.file);
			push(data, (uint16_t)r.segment);
			push(data, (uint32_t)r.segment_offset);
			reloc_size += 15;
		}
	}


	for (int i = 0; i < ss.size(); ++i) {
		auto &s = ss[i];
		if (!s) continue;

		auto tmp = s->data();
		if (tmp.empty()) continue;

		reloc_size += tmp.size() + 6;
		data.push_back(omf::SUPER);
		push(data, ((uint32_t)tmp.size() + 1));
		data.push_back(i);

		data.insert(data.end(), tmp.begin(), tmp.end());
	}

	return reloc_size;
}

void save_bin(const std::string &path, omf::segment &segment) {

	int fd;
	fd = open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_BINARY, 0666);
	if (fd < 0) {
		err(EX_CANTCREAT, "Unable to open %s", path.c_str());
	}

	uint32_t org = segment.org;
	auto &data = segment.data;

	for (auto &r : segment.relocs) {

		uint32_t value = r.value + org;
		value >>= -(int8_t)r.shift;

		unsigned offset = r.offset;
		unsigned size = r.size;
		while (size--) {
			data[offset++] = value & 0xff;
			value >>= 8;
		}
	}

	auto ok = write(fd, data.data(), data.size());
	if (ok < 0) {
		close(fd);
		err(EX_OSERR, "write %s", path.c_str());
	}
	close(fd);
}


void save_omf(const std::string &path, std::vector<omf::segment> &segments, unsigned flags) {

	// expressload doesn't support links to other files. 
	// fortunately, we don't either.

	std::vector<uint8_t> expr_headers;
	std::vector<unsigned> expr_offsets;


	bool compress = !(flags & OMF_NO_COMPRESS);
	bool super = !(flags & OMF_NO_SUPER);
	bool expressload = !(flags & OMF_NO_EXPRESS);
	bool v1 = flags & OMF_V1;

	if (v1) {
		expressload = false;
		super = false;
	}

	int fd;
	fd = open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_BINARY, 0666);
	if (fd < 0) {
		err(EX_CANTCREAT, "Unable to open %s", path.c_str());
	}


	uint32_t offset = 0;
	if (expressload) {
		for (auto &s : segments) {
			s.segnum++;
			for (auto &r : s.intersegs) r.segment++;
		}

		// calculate express load segment size.
		// sizeof includes the trailing 0, so no need to add in byte size.
		offset = sizeof(omf_header) + 10 + sizeof("~ExpressLoad");

		offset += 6; // lconst + end
		offset += 6;  // header.
		for (auto &s : segments) {
			offset += 8 + 2;
			offset += sizeof(omf_express_header) + 10;
			offset += s.segname.length() + 1;
		}

		lseek(fd, offset, SEEK_SET);
	}


	for (auto &s : segments) {
		omf_header h;
		h.length = s.data.size() + s.reserved_space;
		h.kind = s.kind;
		h.banksize = s.data.size() > 0xffff ? 0x0000 : 0x010000;
		h.segnum = s.segnum;
		h.alignment = s.alignment;
		h.reserved_space = s.reserved_space;
		h.org = s.org;

		uint32_t reserved_space = 0;
		if (expressload) {
			std::swap(reserved_space, h.reserved_space);
		}

		// length field INCLUDES reserved space.  Express expand reserved space.


		std::vector<uint8_t> data;

		// push segname and load name onto data.
		// data.insert(data.end(), 10, ' ');
		push(data, s.loadname, 10);
		push(data, s.segname);

		h.dispname = sizeof(omf_header);
		h.dispdata = sizeof(omf_header) + data.size();



		uint32_t lconst_offset = offset + sizeof(omf_header) + data.size() + 5;
		uint32_t lconst_size = s.data.size() + reserved_space;


		//lconst record
		push(data, (uint8_t)omf::LCONST);
		push(data, (uint32_t)lconst_size);

		size_t data_offset = data.size();

		data.insert(data.end(), s.data.begin(), s.data.end());

		if (reserved_space) {
			data.insert(data.end(), reserved_space, 0);
		}

		uint32_t reloc_offset = offset + sizeof(omf_header) + data.size();
		uint32_t reloc_size = 0;

		reloc_size = add_relocs(data, data_offset, s, compress, super);

		// end-of-record
		push(data, (uint8_t)omf::END);

		h.bytecount = data.size() + sizeof(omf_header);

		if (expressload) {

			expr_offsets.emplace_back(expr_headers.size());

			if (lconst_size == 0) lconst_offset = 0;
			if (reloc_size == 0) reloc_offset = 0;


			push(expr_headers, (uint32_t)lconst_offset);
			push(expr_headers, (uint32_t)lconst_size);
			push(expr_headers, (uint32_t)reloc_offset);
			push(expr_headers, (uint32_t)reloc_size);

			push(expr_headers, h.unused1);
			push(expr_headers, h.lablen);
			push(expr_headers, h.numlen);
			push(expr_headers, h.version);
			push(expr_headers, h.banksize);
			push(expr_headers, h.kind);
			push(expr_headers, h.unused2);
			push(expr_headers, h.org);
			push(expr_headers, h.alignment);
			push(expr_headers, h.numsex);
			push(expr_headers, h.unused3);
			push(expr_headers, h.segnum);
			push(expr_headers, h.entry);
			push(expr_headers, (uint16_t)(h.dispname));
			push(expr_headers, h.dispdata);

			expr_headers.insert(expr_headers.end(), 10, ' ');
			push(expr_headers, s.segname);
		}

		if (v1) to_v1(h);
		to_little(h);

		offset += write(fd, &h, sizeof(h));
		offset += write(fd, data.data(), data.size());

		// version 1 needs 512-byte padding for all but final segment.
		if (v1 && &s != &segments.back()) {
			static uint8_t zero[512];
			offset += write(fd, zero, 512 - (offset & 511) );
		}
	}

	if (expressload) {
		omf_header h;
		h.segnum = 1;
		h.banksize = 0x00010000;
		h.kind = 0x8001;
		h.dispname = 0x2c;
		h.dispdata = 0x43;

		unsigned fudge = 10 * segments.size();

		h.length = 6 + expr_headers.size() + fudge;

		std::vector<uint8_t> data;
		data.insert(data.begin(), 10, ' ');
		push(data, std::string("~ExpressLoad"));
		push(data, (uint8_t)0xf2); // lconst.
		push(data, (uint32_t)h.length);

		push(data, (uint32_t)0); // reserved
		push(data, (uint16_t)(segments.size() - 1)); // seg count - 1


		for (auto &offset : expr_offsets) {
			push(data, (uint16_t)(fudge + offset));
			push(data, (uint16_t)0);
			push(data, (uint32_t)0);
			fudge -= 8;
		}

		for (auto &s : segments) {
			push(data, (uint16_t)s.segnum);
		}

		data.insert(data.end(), expr_headers.begin(), expr_headers.end());
		push(data, (uint8_t)0); // end.

		h.bytecount = data.size() + sizeof(omf_header);

		to_little(h);
		lseek(fd, 0, SEEK_SET);
		write(fd, &h, sizeof(h));
		write(fd, data.data(), data.size());

	}

	close(fd);
}
