#pragma once
// Sparse-set ECS. Dense component storage keeps iteration cache friendly,
// which matters because the scene holds tens of thousands of props.
#include "ecs/Entity.hpp"
#include <cassert>
#include <memory>
#include <typeindex>
#include <unordered_map>
#include <vector>

namespace hv::ecs {

class IPool {
public:
    virtual ~IPool() = default;
    virtual void remove(u32 entityIndex) = 0;
    virtual bool has(u32 entityIndex) const = 0;
    virtual size_t size() const = 0;
};

template <typename T>
class Pool final : public IPool {
public:
    T& add(u32 entityIndex, T value) {
        if (entityIndex >= sparse_.size()) sparse_.resize(entityIndex + 1, kInvalid);
        if (sparse_[entityIndex] != kInvalid) {
            dense_[sparse_[entityIndex]] = std::move(value);
            return dense_[sparse_[entityIndex]];
        }
        sparse_[entityIndex] = static_cast<u32>(dense_.size());
        dense_.push_back(std::move(value));
        owners_.push_back(entityIndex);
        return dense_.back();
    }
    T* get(u32 entityIndex) {
        if (entityIndex >= sparse_.size() || sparse_[entityIndex] == kInvalid) return nullptr;
        return &dense_[sparse_[entityIndex]];
    }
    const T* get(u32 entityIndex) const {
        if (entityIndex >= sparse_.size() || sparse_[entityIndex] == kInvalid) return nullptr;
        return &dense_[sparse_[entityIndex]];
    }
    bool has(u32 entityIndex) const override {
        return entityIndex < sparse_.size() && sparse_[entityIndex] != kInvalid;
    }
    void remove(u32 entityIndex) override {
        if (!has(entityIndex)) return;
        const u32 slot = sparse_[entityIndex];
        const u32 last = static_cast<u32>(dense_.size() - 1);
        if (slot != last) {
            dense_[slot] = std::move(dense_[last]);
            owners_[slot] = owners_[last];
            sparse_[owners_[slot]] = slot;
        }
        dense_.pop_back();
        owners_.pop_back();
        sparse_[entityIndex] = kInvalid;
    }
    size_t size() const override { return dense_.size(); }
    std::vector<T>&   data() { return dense_; }
    const std::vector<T>& data() const { return dense_; }
    const std::vector<u32>& owners() const { return owners_; }

private:
    static constexpr u32 kInvalid = 0xFFFFFFFFu;
    std::vector<T>   dense_;
    std::vector<u32> owners_;
    std::vector<u32> sparse_;
};

class Registry {
public:
    Entity create() {
        u32 index;
        if (!free_.empty()) {
            index = free_.back();
            free_.pop_back();
        } else {
            index = static_cast<u32>(generations_.size());
            generations_.push_back(1);
        }
        alive_.resize(std::max<size_t>(alive_.size(), index + 1), 0);
        alive_[index] = 1;
        return Entity::make(index, generations_[index]);
    }

    bool alive(Entity e) const {
        const u32 i = e.index();
        return e.valid() && i < generations_.size() && alive_[i] &&
               generations_[i] == e.generation();
    }

    void destroy(Entity e) {
        if (!alive(e)) return;
        const u32 i = e.index();
        for (auto& [type, pool] : pools_) pool->remove(i);
        alive_[i] = 0;
        // Generation wraps rather than saturating; 10 bits is plenty of churn
        // headroom for a session and a wrapped id is still validated.
        generations_[i] = generations_[i] + 1 == (1u << 10) ? 1 : generations_[i] + 1;
        free_.push_back(i);
    }

    template <typename T>
    T& add(Entity e, T value = T{}) {
        assert(alive(e));
        return pool<T>().add(e.index(), std::move(value));
    }
    template <typename T>
    T* get(Entity e) {
        if (!alive(e)) return nullptr;
        return pool<T>().get(e.index());
    }
    template <typename T>
    const T* get(Entity e) const {
        if (!alive(e)) return nullptr;
        auto it = pools_.find(std::type_index(typeid(T)));
        if (it == pools_.end()) return nullptr;
        return static_cast<const Pool<T>*>(it->second.get())->get(e.index());
    }
    template <typename T>
    bool has(Entity e) const { return get<T>(e) != nullptr; }
    template <typename T>
    void remove(Entity e) { if (alive(e)) pool<T>().remove(e.index()); }

    /// Iterate the dense array of one component type. `fn(Entity, T&)`.
    template <typename T, typename Fn>
    void each(Fn&& fn) {
        Pool<T>& p = pool<T>();
        auto& dense = p.data();
        const auto& owners = p.owners();
        for (size_t i = 0; i < dense.size(); ++i)
            fn(Entity::make(owners[i], generations_[owners[i]]), dense[i]);
    }

    template <typename T>
    Pool<T>& pool() {
        const std::type_index key(typeid(T));
        auto it = pools_.find(key);
        if (it == pools_.end())
            it = pools_.emplace(key, std::make_unique<Pool<T>>()).first;
        return *static_cast<Pool<T>*>(it->second.get());
    }

    size_t entityCount() const {
        size_t n = 0;
        for (u8 a : alive_) n += a;
        return n;
    }
    void clear() {
        pools_.clear();
        generations_.clear();
        alive_.clear();
        free_.clear();
    }

private:
    std::unordered_map<std::type_index, std::unique_ptr<IPool>> pools_;
    std::vector<u32> generations_;
    std::vector<u8>  alive_;
    std::vector<u32> free_;
};

} // namespace hv::ecs
