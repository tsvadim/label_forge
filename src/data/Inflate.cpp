// SPDX-License-Identifier: GPL-3.0-or-later
// Small, dependency-free DEFLATE decoder (canonical Huffman, RFC 1951).
#include "Inflate.h"

#include <cstdint>
#include <cstring>

namespace lf {

namespace {

struct Huffman {
    uint16_t count[16];
    uint16_t symbol[320];
};

class State {
public:
    State(const QByteArray& in, QByteArray& out)
        : m_in(reinterpret_cast<const uint8_t*>(in.constData())), m_len(size_t(in.size())), m_out(out)
    {
    }

    bool run()
    {
        int last;
        do {
            last = bits(1);
            const int type = bits(2);
            if (m_err)
                return false;
            bool ok;
            switch (type) {
            case 0: ok = stored(); break;
            case 1: ok = fixed(); break;
            case 2: ok = dynamic(); break;
            default: ok = false;
            }
            if (!ok || m_err)
                return false;
        } while (!last);
        return true;
    }

private:
    int bits(int need)
    {
        uint32_t val = m_bitbuf;
        while (m_bitcnt < need) {
            if (m_pos >= m_len) {
                m_err = true;
                return 0;
            }
            val |= uint32_t(m_in[m_pos++]) << m_bitcnt;
            m_bitcnt += 8;
        }
        m_bitbuf = val >> need;
        m_bitcnt -= need;
        return int(val & ((1u << need) - 1));
    }

    bool stored()
    {
        m_bitbuf = 0;
        m_bitcnt = 0;
        if (m_pos + 4 > m_len)
            return false;
        const unsigned len = m_in[m_pos] | (m_in[m_pos + 1] << 8);
        const unsigned nlen = m_in[m_pos + 2] | (m_in[m_pos + 3] << 8);
        m_pos += 4;
        if (len != (~nlen & 0xffff) || m_pos + len > m_len)
            return false;
        m_out.append(reinterpret_cast<const char*>(m_in + m_pos), qsizetype(len));
        m_pos += len;
        return true;
    }

    int decode(const Huffman& h)
    {
        int code = 0, first = 0, index = 0;
        for (int len = 1; len < 16; ++len) {
            code |= bits(1);
            if (m_err)
                return -1;
            const int count = h.count[len];
            if (code - count < first)
                return h.symbol[index + (code - first)];
            index += count;
            first += count;
            first <<= 1;
            code <<= 1;
        }
        return -1;
    }

    static bool construct(Huffman& h, const uint8_t* length, int n)
    {
        std::memset(h.count, 0, sizeof(h.count));
        for (int s = 0; s < n; ++s)
            h.count[length[s]]++;
        if (h.count[0] == n)
            return true;
        int left = 1;
        for (int len = 1; len < 16; ++len) {
            left <<= 1;
            left -= h.count[len];
            if (left < 0)
                return false;
        }
        uint16_t offs[16];
        offs[1] = 0;
        for (int len = 1; len < 15; ++len)
            offs[len + 1] = offs[len] + h.count[len];
        for (int s = 0; s < n; ++s)
            if (length[s])
                h.symbol[offs[length[s]]++] = uint16_t(s);
        return true;
    }

    bool codes(const Huffman& lencode, const Huffman& distcode)
    {
        static const uint16_t lbase[29] = {3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
                                           31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
        static const uint8_t lext[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                                         2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
        static const uint16_t dbase[30] = {1,    2,    3,    4,    5,    7,     9,     13,    17,  25,
                                           33,   49,   65,   97,   129,  193,   257,   385,   513, 769,
                                           1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
        static const uint8_t dext[30] = {0, 0, 0, 0, 1, 1, 2, 2,  3,  3,  4,  4,  5,  5,  6,
                                         6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
        for (;;) {
            int sym = decode(lencode);
            if (sym < 0)
                return false;
            if (sym < 256) {
                m_out.append(char(sym));
            } else if (sym == 256) {
                return true;
            } else {
                sym -= 257;
                if (sym >= 29)
                    return false;
                const int len = lbase[sym] + bits(lext[sym]);
                const int dsym = decode(distcode);
                if (dsym < 0 || dsym >= 30)
                    return false;
                const qsizetype dist = dbase[dsym] + bits(dext[dsym]);
                if (m_err || dist > m_out.size())
                    return false;
                const qsizetype from = m_out.size() - dist;
                for (int i = 0; i < len; ++i)
                    m_out.append(m_out.at(from + i));
            }
        }
    }

    bool fixed()
    {
        static Huffman lencode, distcode;
        static bool built = false;
        if (!built) {
            uint8_t lengths[288];
            int s = 0;
            for (; s < 144; ++s) lengths[s] = 8;
            for (; s < 256; ++s) lengths[s] = 9;
            for (; s < 280; ++s) lengths[s] = 7;
            for (; s < 288; ++s) lengths[s] = 8;
            construct(lencode, lengths, 288);
            for (s = 0; s < 30; ++s) lengths[s] = 5;
            construct(distcode, lengths, 30);
            built = true;
        }
        return codes(lencode, distcode);
    }

    bool dynamic()
    {
        static const uint8_t order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
        uint8_t lengths[320];
        const int nlen = bits(5) + 257;
        const int ndist = bits(5) + 1;
        const int ncode = bits(4) + 4;
        if (m_err || nlen > 286 || ndist > 30)
            return false;
        int index;
        for (index = 0; index < ncode; ++index)
            lengths[order[index]] = uint8_t(bits(3));
        for (; index < 19; ++index)
            lengths[order[index]] = 0;
        Huffman lencode, distcode;
        if (!construct(lencode, lengths, 19))
            return false;
        index = 0;
        while (index < nlen + ndist) {
            int sym = decode(lencode);
            if (sym < 0)
                return false;
            if (sym < 16) {
                lengths[index++] = uint8_t(sym);
            } else {
                int len = 0;
                int rep;
                if (sym == 16) {
                    if (index == 0)
                        return false;
                    len = lengths[index - 1];
                    rep = 3 + bits(2);
                } else if (sym == 17) {
                    rep = 3 + bits(3);
                } else {
                    rep = 11 + bits(7);
                }
                if (index + rep > nlen + ndist)
                    return false;
                while (rep--)
                    lengths[index++] = uint8_t(len);
            }
        }
        if (lengths[256] == 0)
            return false;
        if (!construct(lencode, lengths, nlen))
            return false;
        if (!construct(distcode, lengths + nlen, ndist))
            return false;
        return codes(lencode, distcode);
    }

    const uint8_t* m_in;
    size_t m_len;
    size_t m_pos = 0;
    uint32_t m_bitbuf = 0;
    int m_bitcnt = 0;
    bool m_err = false;
    QByteArray& m_out;
};

} // namespace

bool inflateRaw(const QByteArray& in, QByteArray* out, qsizetype expectedSize)
{
    out->clear();
    if (expectedSize > 0)
        out->reserve(expectedSize);
    State s(in, *out);
    return s.run();
}

} // namespace lf
