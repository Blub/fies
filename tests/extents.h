#ifndef FIES_TESTS_EXTENTS_H
#define FIES_TESTS_EXTENTS_H

static inline constexpr uint32_t
operator"" _freg(unsigned long long perms)
{
	return cast<uint32_t>(perms) | FIES_M_FREG;
}

static inline constexpr uint32_t
mkexfl(const char *txt, unsigned long i, unsigned long len)
{
	return (txt[i] == 'd' ? FIES_FL_DATA :
	        txt[i] == 'z' ? FIES_FL_ZERO :
	        txt[i] == 'h' ? FIES_FL_HOLE :
	        txt[i] == 'c' ? FIES_FL_COPY :
	        txt[i] == 's' ? FIES_FL_SHARED :
	        0
	       ) | (
	        (i == len) ? 0 : mkexfl(txt, i+1, len)
	       );
}

static inline constexpr uint32_t
operator""_exfl(const char *txt, unsigned long len)
{
	return mkexfl(txt, 0, len);
}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wpadded"
struct PhyExt {
	PhyExt() = delete;

	constexpr PhyExt(fies_pos phy_,
	                 fies_sz len_,
	                 uint32_t flags_,
	                 fies_pos dev_ = 0)
		: dev(dev_)
		, phy(phy_)
		, len(len_)
		, flags(flags_)
	{}

	fies_pos dev;
	fies_pos phy;
	fies_sz  len;
	uint32_t flags;
};
#pragma clang diagnostic pop

inline constexpr FiesFile_Extent
extent(fies_pos log, fies_pos phy, fies_sz len, uint32_t flags, fies_pos dev=0)
{
	return { dev, log, phy, len, flags, {cast<fies_id>(-1), 0} };
}

inline constexpr FiesFile_Extent
extent(fies_pos log, PhyExt p)
{
	return extent(log, p.phy, p.len, p.flags, p.dev);
}

#endif
