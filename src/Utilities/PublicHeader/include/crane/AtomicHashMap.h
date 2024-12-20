/**
 * Copyright (c) 2024 Peking University and Peking University
 * Changsha Institute for Computing and Digital Economy
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as
 * published by the Free Software Foundation, either version 3 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

#include <absl/container/flat_hash_map.h>

#include "Lock.h"
#include "Pointer.h"

namespace util {

template <typename T>
class Synchronized {
 private:
  T value_;
  mutable mutex mutex_;

 public:
  // Only implement exclusive lock protected value
  using ExclusivePtr = util::ScopeExclusivePtr<T, mutex>;
  using ConstExclusivePtr = util::ScopeExclusivePtr<const T, mutex>;

  explicit Synchronized(T const& value) : value_(value) {}
  explicit Synchronized(T&& value) : value_(std::move(value)) {}

  Synchronized(Synchronized&& other) noexcept {
    lock_guard lock(other.mutex_);
    value_ = std::move(other.value_);
  }

  Synchronized& operator=(Synchronized&& other) noexcept {
    if (this != &other) {
      lock_guard lock(other.mutex_);
      value_ = std::move(other.value_);
    }
    return *this;
  }

  Synchronized(const Synchronized& other) = delete;
  Synchronized& operator=(const Synchronized& other) = delete;

  mutex& Mutex() const { return mutex_; }

  ExclusivePtr GetExclusivePtr() {
    mutex_.Lock();
    return ExclusivePtr{&value_, &mutex_};
  }

  ConstExclusivePtr GetExclusivePtr() const {
    mutex_.Lock();
    return ConstExclusivePtr{&value_, &mutex_};
  }

  T* RawPtr() { return &value_; }
};

template <template <typename...> class MapType, typename Key, typename T>
class AtomicHashMap {
 public:
  class CombinedLock;

  using RawMap = MapType<Key, Synchronized<T>>;

  using ValueExclusivePtr = util::ManagedScopeExclusivePtr<T, CombinedLock>;

  using MapExclusivePtr =
      util::ScopeExclusivePtr<MapType<Key, Synchronized<T>>, rw_mutex>;

  using MapSharedPtr =
      util::ScopeSharedPtr<MapType<Key, Synchronized<T>>, rw_mutex>;

  using MapConstSharedPtr =
      util::ScopeConstSharedPtr<MapType<Key, Synchronized<T>>, rw_mutex>;

  class CombinedLock {
   private:
    rw_mutex* global_map_shared_mutex_;
    mutex* value_mutex_;

   public:
    CombinedLock() = default;

    CombinedLock(rw_mutex* rw_mtx, mutex* mtx)
        : global_map_shared_mutex_(rw_mtx), value_mutex_(mtx) {}

    CombinedLock(const CombinedLock&) = delete;
    CombinedLock& operator=(const CombinedLock&) = delete;

    CombinedLock(CombinedLock&& val) noexcept {
      global_map_shared_mutex_ = val.global_map_shared_mutex_;
      value_mutex_ = val.value_mutex_;

      val.global_map_shared_mutex_ = nullptr;
      val.value_mutex_ = nullptr;
    }

    CombinedLock& operator=(CombinedLock&& val) noexcept {
      if (this != &val) {
        global_map_shared_mutex_ = val.global_map_shared_mutex_;
        value_mutex_ = val.value_mutex_;

        val.global_map_shared_mutex_ = nullptr;
        val.value_mutex_ = nullptr;
      }

      return *this;
    };

    void unlock() {
      global_map_shared_mutex_->unlock_shared();
      value_mutex_->Unlock();
    }
  };

  AtomicHashMap() = default;

  // This function should be called only once!
  void InitFromMap(MapType<Key, T>&& other_map) {
    m_value_map_.reserve(other_map.size());
    for (auto& [k, v] : other_map) {
      // Using the emplace method and
      // std::move for movement construction in site
      m_value_map_.emplace(k, std::move(v));
    }
  }

  bool Contains(const Key& key) {
    read_lock_guard lock_guard(m_global_rw_mutex_);
    return m_value_map_.contains(key);
  }

  ValueExclusivePtr GetValueExclusivePtr(const Key& key) {
    m_global_rw_mutex_.lock_shared();
    auto iter = m_value_map_.find(key);

    if (iter == m_value_map_.end()) {
      m_global_rw_mutex_.unlock_shared();
      return ValueExclusivePtr{};
    } else {
      iter->second.Mutex().Lock();
      CombinedLock combined_lock(&m_global_rw_mutex_, &iter->second.Mutex());
      return ValueExclusivePtr{iter->second.RawPtr(), std::move(combined_lock)};
    }
  }

  ValueExclusivePtr operator[](const Key& key) {
    return GetValueExclusivePtr(key);
  }

  MapConstSharedPtr GetMapConstSharedPtr() {
    m_global_rw_mutex_.lock_shared();
    return MapConstSharedPtr{&m_value_map_, &m_global_rw_mutex_};
  }

  MapSharedPtr GetMapSharedPtr() {
    m_global_rw_mutex_.lock_shared();
    return MapSharedPtr{&m_value_map_, &m_global_rw_mutex_};
  }

  MapExclusivePtr GetMapExclusivePtr() {
    m_global_rw_mutex_.lock();
    return MapExclusivePtr{&m_value_map_, &m_global_rw_mutex_};
  }

  template <typename... Args>
  void Emplace(Args&&... args) {
    m_global_rw_mutex_.lock();
    m_value_map_.emplace(std::forward<Args>(args)...);
    m_global_rw_mutex_.unlock();
  }

  void Erase(const Key& key) {
    m_global_rw_mutex_.lock();
    m_value_map_.erase(key);
    m_global_rw_mutex_.unlock();
  }

 private:
  RawMap m_value_map_;
  rw_mutex m_global_rw_mutex_;
};

}  // namespace util
