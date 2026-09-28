#pragma once

#include <algorithm>
#include <cstdint>
#include <iomanip>
#include <ostream>
#include <sstream>
#include <string>
#include <vector>


namespace analysis
{

class BigUInt
{
public:

    BigUInt(
        std::uint64_t value = 0
    )
    {
        assign(
            value
        );
    }


    BigUInt&
    operator+=(
        const BigUInt& other
    )
    {
        const std::size_t size =
            std::max(
                digits_.size(),
                other.digits_.size()
            );


        digits_.resize(
            size,
            0
        );


        std::uint64_t carry =
            0;


        for (
            std::size_t i = 0;
            i < size;
            ++i
        )
        {
            const std::uint64_t left =
                digits_[i];


            const std::uint64_t right =
                (
                    i < other.digits_.size()
                )
                ?
                other.digits_[i]
                :
                0;


            const std::uint64_t sum =
                left
                +
                right
                +
                carry;


            digits_[i] =
                static_cast<std::uint32_t>(
                    sum
                    %
                    kBase
                );


            carry =
                sum
                /
                kBase;
        }


        if (carry != 0)
        {
            digits_.push_back(
                static_cast<std::uint32_t>(
                    carry
                )
            );
        }


        normalize();


        return *this;
    }


    friend BigUInt operator+(
        BigUInt left,
        const BigUInt& right
    )
    {
        left +=
            right;

        return left;
    }


    friend BigUInt operator*(
        const BigUInt& left,
        const BigUInt& right
    )
    {
        if (
            left.isZero()
            ||
            right.isZero()
        )
        {
            return BigUInt(0);
        }


        BigUInt result;


        result.digits_.assign(
            left.digits_.size()
            +
            right.digits_.size(),
            0
        );


        for (
            std::size_t i = 0;
            i < left.digits_.size();
            ++i
        )
        {
            std::uint64_t carry =
                0;


            for (
                std::size_t j = 0;
                j < right.digits_.size();
                ++j
            )
            {
                const std::uint64_t current =
                    static_cast<std::uint64_t>(
                        result.digits_[
                            i + j
                        ]
                    )
                    +
                    static_cast<std::uint64_t>(
                        left.digits_[i]
                    )
                    *
                    static_cast<std::uint64_t>(
                        right.digits_[j]
                    )
                    +
                    carry;


                result.digits_[
                    i + j
                ] =
                    static_cast<std::uint32_t>(
                        current
                        %
                        kBase
                    );


                carry =
                    current
                    /
                    kBase;
            }


            std::size_t position =
                i
                +
                right.digits_.size();


            while (carry != 0)
            {
                if (
                    position
                    >=
                    result.digits_.size()
                )
                {
                    result.digits_.push_back(
                        0
                    );
                }


                const std::uint64_t current =
                    static_cast<std::uint64_t>(
                        result.digits_[
                            position
                        ]
                    )
                    +
                    carry;


                result.digits_[
                    position
                ] =
                    static_cast<std::uint32_t>(
                        current
                        %
                        kBase
                    );


                carry =
                    current
                    /
                    kBase;


                ++position;
            }
        }


        result.normalize();


        return result;
    }


    friend bool operator==(
        const BigUInt& left,
        const BigUInt& right
    )
    {
        return
            left.digits_
            ==
            right.digits_;
    }


    friend bool operator!=(
        const BigUInt& left,
        const BigUInt& right
    )
    {
        return
            !(
                left
                ==
                right
            );
    }


    std::string toString() const
    {
        if (digits_.empty())
        {
            return "0";
        }


        std::ostringstream stream;


        stream
            <<
            digits_.back();


        for (
            std::size_t i =
                digits_.size() - 1;
            i > 0;
            --i
        )
        {
            stream
                <<
                std::setw(9)
                <<
                std::setfill('0')
                <<
                digits_[i - 1];
        }


        return
            stream.str();
    }


    friend std::ostream& operator<<(
        std::ostream& stream,
        const BigUInt& value
    )
    {
        stream
            <<
            value.toString();

        return stream;
    }


private:

    static constexpr std::uint64_t
        kBase =
            1000000000ULL;


    std::vector<std::uint32_t>
        digits_;


    void assign(
        std::uint64_t value
    )
    {
        digits_.clear();


        do
        {
            digits_.push_back(
                static_cast<std::uint32_t>(
                    value
                    %
                    kBase
                )
            );


            value /=
                kBase;

        } while (value != 0);
    }


    bool isZero() const
    {
        return
            digits_.size() == 1
            &&
            digits_[0] == 0;
    }


    void normalize()
    {
        while (
            digits_.size() > 1
            &&
            digits_.back() == 0
        )
        {
            digits_.pop_back();
        }
    }
};

}
