#pragma once

#include <atomic>
#include <string_view>
#include <cstddef>
#include <limits>

#include "air/lightmdb/core.hpp"

namespace air
{
    namespace lightmdb
    {
        namespace fixed
        {
            template <typename T, bool IsLock = true>
            class table
            {
            public:
                using value_type = T;
                using size_type = std::size_t;
                using difference_type = std::ptrdiff_t;
                using reference = value_type &;
                using const_reference = const value_type &;
                using pointer = value_type *;
                using const_pointer = const value_type *;

            private:
                struct node
                {
                    detail::atomic<bool> is_value;
                    value_type value;

                    node &operator=(const value_type &val)
                    {
                        this->value = val;
                        this->is_value = true;
                        this->is_value.notify_all();
                        return *this;
                    }

                    void wait()
                    {
                        this->is_value.wait(false);
                    }
                };

                detail::mmap mmap_;
                node *node_;

                // 本地 capacity
                std::size_t capacity_;

                void remmap()
                {
                    capacity_ = mmap_.get_header().capacity / sizeof(node);
                    mmap_.remmap();
                    node_ = static_cast<node *>(mmap_.get_address());
                }

                /// 推入数据
                std::size_t do_push(const value_type &val, std::size_t index)
                {
                    if (index >= capacity_)
                    {
                        auto &header = mmap_.get_header();

                        if constexpr (IsLock == true)
                        {
                            auto flag = header.lock.exchange(true);

                            if (!flag)
                            {
                                if (capacity_ == this->capacity())
                                {
                                    mmap_.recapacity();
                                }

                                header.lock = false;
                                header.capacity.notify_all();
                            }

                            header.capacity.wait(capacity_ * sizeof(node));
                        }
                        else
                        {
                            mmap_.recapacity();
                            header.capacity.notify_all();
                        }

                        this->remmap();
                    }

                    node_[index] = val;
                    return index;
                }

                /// 读取数据
                node &do_read(std::size_t index)
                {
                    while (index >= capacity_)
                    {
                        auto &header = mmap_.get_header();
                        header.capacity.wait(capacity_ * sizeof(node));

                        this->remmap();
                    }
                    return node_[index];
                }

            public:
                table(std::string_view name, mode_t mode, std::size_t capacity)
                    : mmap_(name, mode, capacity * sizeof(node))
                {
                    node_ = static_cast<node *>(mmap_.get_address());
                    capacity_ = this->capacity();
                }

                table(std::string_view name, mode_t mode)
                    : mmap_(name, mode)
                {
                    node_ = static_cast<node *>(mmap_.get_address());
                    capacity_ = this->capacity();
                }

                ~table() = default;

                std::size_t push(const value_type &val)
                {
                    auto index = mmap_.get_header().size.fetch_add(sizeof(node));
                    return this->do_push(val, index / sizeof(node));
                }

                value_type &operator[](std::size_t index)
                {
                    return this->do_read(index).value;
                }

                const value_type &operator[](std::size_t index) const
                {
                    return const_cast<table *>(this)->operator[](index);
                }

                bool has_value(std::size_t index) const
                {
                    if (index < this->size())
                        return const_cast<table *>(this)->do_read(index).is_value;
                    else
                        return false;
                }

                void wait(std::size_t index) const
                {
                    const_cast<table *>(this)->do_read(index).wait();
                }

                bool empty() const
                {
                    return !this->size();
                }

                std::size_t size() const
                {
                    return mmap_.size() / sizeof(node);
                }

                std::size_t max_size() const
                {
                    return std::numeric_limits<std::size_t>::max();
                }

                std::size_t capacity() const
                {
                    return mmap_.capacity() / sizeof(node);
                }

                void shrink_to_fit()
                {
                    mmap_.shrink_to_fit();
                }

                const std::string &name() const
                {
                    return mmap_.name();
                }
            };
        }
    }
}