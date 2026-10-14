#include "abc/Inflate.hpp"

#include <array>

namespace as3lsp::abc {

namespace {

constexpr int maxBits = 15;
constexpr int maxLengthCodes = 286;
constexpr int maxDistanceCodes = 30;
constexpr int fixedLengthCodes = 288;

struct InflateError {};

// Canonical Huffman decoding table, in the compact form used by zlib's "puff" reference decoder.
struct Huffman {
    std::array<std::int16_t, maxBits + 1> count{};
    std::array<std::int16_t, fixedLengthCodes> symbol{};

    // Returns 0 for a complete code, a positive value for an incomplete one, a negative value if
    // oversubscribed.
    int build(std::span<const std::int16_t> lengths) {
        count.fill(0);
        for (std::int16_t length : lengths) {
            ++count[static_cast<std::size_t>(length)];
        }
        if (count[0] == static_cast<std::int16_t>(lengths.size())) {
            return 0;
        }
        int left = 1;
        for (int len = 1; len <= maxBits; ++len) {
            left <<= 1;
            left -= count[static_cast<std::size_t>(len)];
            if (left < 0) {
                return left;
            }
        }
        std::array<std::int16_t, maxBits + 1> offsets{};
        for (int len = 1; len < maxBits; ++len) {
            offsets[static_cast<std::size_t>(len + 1)] = static_cast<std::int16_t>(
                offsets[static_cast<std::size_t>(len)] + count[static_cast<std::size_t>(len)]);
        }
        for (std::size_t sym = 0; sym < lengths.size(); ++sym) {
            if (lengths[sym] != 0) {
                symbol[static_cast<std::size_t>(offsets[static_cast<std::size_t>(lengths[sym])]++)] =
                    static_cast<std::int16_t>(sym);
            }
        }
        return left;
    }
};

class Inflater {
public:
    Inflater(std::span<const std::uint8_t> input, std::size_t sizeHint) : input_(input) {
        output_.reserve(sizeHint);
    }

    std::vector<std::uint8_t> run() {
        bool last = false;
        while (!last) {
            last = bits(1) != 0;
            switch (bits(2)) {
            case 0:
                stored();
                break;
            case 1:
                fixed();
                break;
            case 2:
                dynamic();
                break;
            default:
                throw InflateError{};
            }
        }
        return std::move(output_);
    }

private:
    int bits(int need) {
        std::uint32_t value = bitBuffer_;
        while (bitCount_ < need) {
            if (position_ >= input_.size()) {
                throw InflateError{};
            }
            value |= static_cast<std::uint32_t>(input_[position_++]) << bitCount_;
            bitCount_ += 8;
        }
        bitBuffer_ = value >> need;
        bitCount_ -= need;
        return static_cast<int>(value & ((1U << need) - 1));
    }

    void stored() {
        bitBuffer_ = 0;
        bitCount_ = 0;
        if (position_ + 4 > input_.size()) {
            throw InflateError{};
        }
        const std::size_t length = input_[position_] | (input_[position_ + 1] << 8);
        const std::size_t complement = input_[position_ + 2] | (input_[position_ + 3] << 8);
        if (length != (~complement & 0xFFFF)) {
            throw InflateError{};
        }
        position_ += 4;
        if (position_ + length > input_.size()) {
            throw InflateError{};
        }
        output_.insert(output_.end(), input_.begin() + static_cast<std::ptrdiff_t>(position_),
                       input_.begin() + static_cast<std::ptrdiff_t>(position_ + length));
        position_ += length;
    }

    int decode(const Huffman& huffman) {
        int code = 0;
        int first = 0;
        int index = 0;
        for (int len = 1; len <= maxBits; ++len) {
            code |= bits(1);
            const int count = huffman.count[static_cast<std::size_t>(len)];
            if (code - count < first) {
                return huffman.symbol[static_cast<std::size_t>(index + (code - first))];
            }
            index += count;
            first += count;
            first <<= 1;
            code <<= 1;
        }
        throw InflateError{};
    }

    void codes(const Huffman& lengthCode, const Huffman& distanceCode) {
        static constexpr std::array<std::int16_t, 29> lengthBase = {
            3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
            31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
        static constexpr std::array<std::int16_t, 29> lengthExtra = {
            0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
        static constexpr std::array<std::int16_t, 30> distanceBase = {
            1,   2,   3,   4,   5,   7,    9,    13,   17,   25,   33,   49,   65,    97,    129,
            193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
        static constexpr std::array<std::int16_t, 30> distanceExtra = {
            0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

        while (true) {
            int symbol = decode(lengthCode);
            if (symbol < 256) {
                output_.push_back(static_cast<std::uint8_t>(symbol));
                continue;
            }
            if (symbol == 256) {
                return;
            }
            symbol -= 257;
            if (symbol >= 29) {
                throw InflateError{};
            }
            const auto lengthIndex = static_cast<std::size_t>(symbol);
            const std::size_t length =
                static_cast<std::size_t>(lengthBase[lengthIndex] + bits(lengthExtra[lengthIndex]));
            const auto distanceIndex = static_cast<std::size_t>(decode(distanceCode));
            if (distanceIndex >= distanceBase.size()) {
                throw InflateError{};
            }
            const std::size_t distance =
                static_cast<std::size_t>(distanceBase[distanceIndex] + bits(distanceExtra[distanceIndex]));
            if (distance > output_.size()) {
                throw InflateError{};
            }
            const std::size_t from = output_.size() - distance;
            for (std::size_t i = 0; i < length; ++i) {
                output_.push_back(output_[from + i]);
            }
        }
    }

    void fixed() {
        if (!fixedReady_) {
            std::array<std::int16_t, fixedLengthCodes> lengths{};
            std::size_t symbol = 0;
            for (; symbol < 144; ++symbol) {
                lengths[symbol] = 8;
            }
            for (; symbol < 256; ++symbol) {
                lengths[symbol] = 9;
            }
            for (; symbol < 280; ++symbol) {
                lengths[symbol] = 7;
            }
            for (; symbol < fixedLengthCodes; ++symbol) {
                lengths[symbol] = 8;
            }
            fixedLength_.build(lengths);
            std::array<std::int16_t, maxDistanceCodes> distances{};
            distances.fill(5);
            fixedDistance_.build(distances);
            fixedReady_ = true;
        }
        codes(fixedLength_, fixedDistance_);
    }

    void dynamic() {
        static constexpr std::array<std::size_t, 19> order = {16, 17, 18, 0, 8,  7, 9,  6, 10, 5,
                                                              11, 4,  12, 3, 13, 2, 14, 1, 15};
        const int lengthCount = bits(5) + 257;
        const int distanceCount = bits(5) + 1;
        const int codeCount = bits(4) + 4;
        if (lengthCount > maxLengthCodes || distanceCount > maxDistanceCodes) {
            throw InflateError{};
        }
        std::array<std::int16_t, maxLengthCodes + maxDistanceCodes> lengths{};
        for (int i = 0; i < codeCount; ++i) {
            lengths[order[static_cast<std::size_t>(i)]] = static_cast<std::int16_t>(bits(3));
        }
        Huffman lengthCode;
        if (lengthCode.build(std::span{lengths}.first(19)) != 0) {
            throw InflateError{};
        }
        const int total = lengthCount + distanceCount;
        int index = 0;
        while (index < total) {
            int symbol = decode(lengthCode);
            if (symbol < 16) {
                lengths[static_cast<std::size_t>(index++)] = static_cast<std::int16_t>(symbol);
                continue;
            }
            std::int16_t repeated = 0;
            if (symbol == 16) {
                if (index == 0) {
                    throw InflateError{};
                }
                repeated = lengths[static_cast<std::size_t>(index - 1)];
                symbol = 3 + bits(2);
            } else if (symbol == 17) {
                symbol = 3 + bits(3);
            } else {
                symbol = 11 + bits(7);
            }
            if (index + symbol > total) {
                throw InflateError{};
            }
            while (symbol-- > 0) {
                lengths[static_cast<std::size_t>(index++)] = repeated;
            }
        }
        if (lengths[256] == 0) {
            throw InflateError{};
        }
        const auto all = std::span{lengths};
        Huffman literals;
        const int literalResult = literals.build(all.first(static_cast<std::size_t>(lengthCount)));
        if (literalResult < 0 ||
            (literalResult > 0 && lengthCount != literals.count[0] + literals.count[1])) {
            throw InflateError{};
        }
        Huffman distances;
        const int distanceResult = distances.build(
            all.subspan(static_cast<std::size_t>(lengthCount), static_cast<std::size_t>(distanceCount)));
        if (distanceResult < 0 ||
            (distanceResult > 0 && distanceCount != distances.count[0] + distances.count[1])) {
            throw InflateError{};
        }
        codes(literals, distances);
    }

    std::span<const std::uint8_t> input_;
    std::size_t position_ = 0;
    std::uint32_t bitBuffer_ = 0;
    int bitCount_ = 0;
    std::vector<std::uint8_t> output_;
    Huffman fixedLength_;
    Huffman fixedDistance_;
    bool fixedReady_ = false;
};

} // namespace

std::optional<std::vector<std::uint8_t>> inflate(std::span<const std::uint8_t> input, std::size_t sizeHint) {
    try {
        return Inflater{input, sizeHint}.run();
    } catch (const InflateError&) {
        return std::nullopt;
    }
}

std::optional<std::vector<std::uint8_t>> zlibDecompress(std::span<const std::uint8_t> input,
                                                        std::size_t sizeHint) {
    if (input.size() < 2 || (input[0] & 0x0F) != 8 || ((input[0] << 8) | input[1]) % 31 != 0 ||
        (input[1] & 0x20)) {
        return std::nullopt;
    }
    return inflate(input.subspan(2), sizeHint);
}

} // namespace as3lsp::abc
