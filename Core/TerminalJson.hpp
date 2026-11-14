//
// Terminal / Core / TerminalJson.hpp
// 轻量 Json 适配层：底层统一使用 ToolHead/DataStructure/Json 的 JsonCore。
//
// 为什么需要这一层：
//   - JsonCore 在 namespace JSON 中提供 JsonElement；
//   - 工程现有组件/管理器长期使用 `Json::parse`、`js.get(...)`、`js.GetArray()` 这类
//     旧 wjson 风格接口；
//   - 直接把 Json 换成 JsonElement 会改动大量调用点。
// 这里在 Terminal 命名空间内提供 `Json` 包装类，内部完全由 JsonCore 承载，
// 对外保留旧接口，做到“整体迁移但调用点不变”。
//
// 注意：JsonCore 有 .cpp（JsonCore.cpp / Number.cpp），使用本头文件时必须一起编译/链接。
//

#pragma once

#include <iterator>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "../../DataStructure/Json/JsonCore.hpp"

namespace Terminal {

    class JsonArrayView;
    class Json;

    //------------------------------------------------------------------
    // Json：JsonCore::JsonElement 的 wjson 兼容包装
    //------------------------------------------------------------------
    class Json {
    public:
        using Element = ::JSON::JsonElement;

        Json()
            : m_owner(std::make_shared<Element>()), m_ref(m_owner.get()) {}

        Json(const Element& e)
            : m_owner(std::make_shared<Element>(e)), m_ref(m_owner.get()) {}
        Json(Element&& e)
            : m_owner(std::make_shared<Element>(std::move(e))), m_ref(m_owner.get()) {}

        Json(const bool v) : Json(Element(v)) {}
        Json(const double v) : Json(Element(v)) {}
        Json(const int v) : Json(Element(v)) {}
        Json(const long long v) : Json(Element(static_cast<long long>(v))) {}
        Json(const std::string& v) : Json(Element(v)) {}
        Json(const char* v) : Json(Element(v)) {}

        Json(const std::unordered_map<std::string, Json>& map)
            : Json(Element(::JSON::UnorderedObject{})) {
            for (const auto& kv : map) (*m_ref)[kv.first] = kv.second.element();
        }

        Json(const std::vector<Json>& list)
            : Json(Element(::JSON::JsonArray{})) {
            auto* arr = m_ref->asArray();
            for (const auto& v : list) arr->push_back(v.element());
        }

        Json(const Json& other) {
            if (other.m_view) {
                m_owner = other.m_owner;
                m_ref = other.m_ref;
                m_view = true;
            } else {
                m_owner = std::make_shared<Element>(*other.m_ref);
                m_ref = m_owner.get();
            }
        }
        Json(Json&&) noexcept = default;
        Json& operator=(const Json& other) {
            if (this != &other && m_ref != other.m_ref) *m_ref = *other.m_ref;
            return *this;
        }
        Json& operator=(Json&& other) noexcept {
            if (this != &other && m_ref != other.m_ref) *m_ref = std::move(*other.m_ref);
            return *this;
        }

        Json& operator=(const Element& e) { *m_ref = e; return *this; }
        Json& operator=(Element&& e) { *m_ref = std::move(e); return *this; }
        Json& operator=(const bool v) { *m_ref = v; return *this; }
        Json& operator=(const double v) { *m_ref = v; return *this; }
        Json& operator=(const int v) { *m_ref = v; return *this; }
        Json& operator=(const std::string& v) { *m_ref = v; return *this; }
        Json& operator=(const char* v) { *m_ref = v; return *this; }

        //--------------------------------------------------------------
        // JsonCore 原生入口
        //--------------------------------------------------------------
        [[nodiscard]] Element& element() noexcept { return *m_ref; }
        [[nodiscard]] const Element& element() const noexcept { return *m_ref; }

        static Json parse(const std::string& text) {
            try {
                return Json(::JSON::Parse(text));
            } catch (...) {
                return Json();
            }
        }

        bool ReadFromFile(const std::string& path) {
            try {
                *m_ref = ::JSON::ParseFile(path.c_str());
                return true;
            } catch (...) {
                *m_ref = Element();
                return false;
            }
        }

        //--------------------------------------------------------------
        // 类型判断（wjson 风格）
        //--------------------------------------------------------------
        [[nodiscard]] bool IsNull() const { return m_ref->isNull(); }
        [[nodiscard]] bool IsBool() const { return m_ref->isBool(); }
        [[nodiscard]] bool IsNumber() const { return m_ref->isNumber(); }
        [[nodiscard]] bool IsString() const { return m_ref->isString(); }
        [[nodiscard]] bool IsArray() const { return m_ref->isArray(); }
        [[nodiscard]] bool IsObject() const {
            return m_ref->isUnorderedObject() || m_ref->isOrderedObject();
        }

        //--------------------------------------------------------------
        // 取值
        //--------------------------------------------------------------
        [[nodiscard]] std::string toStr() const {
            if (m_ref->isString()) return m_ref->getString();
            return toString();
        }
        [[nodiscard]] std::string toString() const {
            if (m_ref->isString()) return m_ref->getString();
            if (m_ref->isBool()) return m_ref->getBool() ? "true" : "false";
            if (m_ref->isNumber()) return std::to_string(m_ref->getNumber());
            return {};
        }
        [[nodiscard]] double toNumber() const {
            if (m_ref->isNumber()) return m_ref->getNumber();
            if (m_ref->isBool()) return m_ref->getBool() ? 1.0 : 0.0;
            if (m_ref->isString()) {
                try { return std::stod(m_ref->getString()); } catch (...) { return 0.0; }
            }
            return 0.0;
        }
        [[nodiscard]] bool toBoolean() const {
            if (m_ref->isBool()) return m_ref->getBool();
            if (m_ref->isNumber()) return m_ref->getNumber() != 0.0;
            if (m_ref->isString()) {
                const auto& s = m_ref->getString();
                return s == "true" || s == "1" || s == "yes" || s == "on";
            }
            return false;
        }

        [[nodiscard]] std::string GetString() const { return m_ref->isString() ? m_ref->getString() : std::string{}; }
        [[nodiscard]] double GetNumber() const { return toNumber(); }
        [[nodiscard]] bool GetBool() const { return toBoolean(); }

        //--------------------------------------------------------------
        // 对象 / 数组访问
        //--------------------------------------------------------------
        [[nodiscard]] bool contains_key(const std::string& key) const {
            if (const auto* u = m_ref->asUnorderedObject()) return u->contains(key);
            if (const auto* o = m_ref->asOrderedObject()) return o->contains(key);
            return false;
        }

        [[nodiscard]] Json get(const std::string& key) const {
            if (const auto* u = m_ref->asUnorderedObject()) {
                const auto it = u->find(key);
                if (it == u->end()) return Json();
                return Json(it->second);
            }
            if (const auto* o = m_ref->asOrderedObject()) {
                const auto it = o->find(key);
                if (it == o->end()) return Json();
                return Json(it->second);
            }
            return Json();
        }

        [[nodiscard]] Json get(const std::size_t index) const {
            if (const auto* a = m_ref->asArray()) {
                if (index >= a->size()) return Json();
                return Json((*a)[index]);
            }
            return Json();
        }

        Json operator[](const std::string& key) {
            if (!IsObject()) *m_ref = ::JSON::UnorderedObject{};
            if (auto* u = m_ref->asUnorderedObject()) return Json(m_owner, &(*u)[key]);
            auto* o = m_ref->asOrderedObject();
            return Json(m_owner, &(*o)[key]);
        }

        Json operator[](const std::size_t index) {
            if (!IsArray()) *m_ref = ::JSON::JsonArray{};
            auto* a = m_ref->asArray();
            if (index >= a->size()) a->resize(index + 1);
            return Json(m_owner, &(*a)[index]);
        }

        [[nodiscard]] std::size_t size() const {
            if (const auto* a = m_ref->asArray()) return a->size();
            if (const auto* u = m_ref->asUnorderedObject()) return u->size();
            if (const auto* o = m_ref->asOrderedObject()) return o->size();
            return 0;
        }

        [[nodiscard]] std::string dump(const int indent = -1) const { return m_ref->dump(indent); }

        JsonArrayView GetArray();
        [[nodiscard]] JsonArrayView GetArray() const;

    private:
        Json(std::shared_ptr<Element> owner, Element* ref)
            : m_owner(ref ? std::move(owner) : std::make_shared<Element>()),
              m_ref(ref ? ref : m_owner.get()),
              m_view(ref != nullptr) {}

        std::shared_ptr<Element> m_owner;
        Element* m_ref{ nullptr };
        bool m_view{ false };

        friend class JsonArrayView;
    };

    //------------------------------------------------------------------
    // 数组视图：只做转发，元素仍指向 JsonCore 内部存储
    //------------------------------------------------------------------
    class JsonArrayView {
    public:
        JsonArrayView(std::shared_ptr<Json::Element> owner, ::JSON::JsonArray* arr)
            : m_owner(std::move(owner)), m_arr(arr) {}

        [[nodiscard]] std::size_t size() const { return m_arr ? m_arr->size() : 0; }
        [[nodiscard]] bool empty() const { return size() == 0; }
        [[nodiscard]] Json operator[](const std::size_t i) const {
            return Json(m_owner, m_arr && i < m_arr->size() ? &(*m_arr)[i] : nullptr);
        }
        void push_back(const Json& v) {
            if (!m_arr) return;
            m_arr->push_back(v.element());
        }
        void clear() { if (m_arr) m_arr->clear(); }

        struct iterator {
            using iterator_category = std::forward_iterator_tag;
            using value_type = Json;
            using difference_type = std::ptrdiff_t;
            using pointer = void;
            using reference = Json;

            std::shared_ptr<Json::Element> owner;
            ::JSON::JsonArray* arr{ nullptr };
            std::size_t idx{ 0 };

            [[nodiscard]] bool operator==(const iterator& o) const { return idx == o.idx && arr == o.arr; }
            [[nodiscard]] bool operator!=(const iterator& o) const { return !(*this == o); }
            iterator& operator++() { ++idx; return *this; }
            [[nodiscard]] Json operator*() const { return Json((*arr)[idx]); }
        };

        [[nodiscard]] iterator begin() const { return { m_owner, m_arr, 0 }; }
        [[nodiscard]] iterator end() const { return { m_owner, m_arr, size() }; }

    private:
        std::shared_ptr<Json::Element> m_owner;
        ::JSON::JsonArray* m_arr{ nullptr };
    };

    inline JsonArrayView Json::GetArray() {
        if (!m_ref->isArray()) *m_ref = ::JSON::JsonArray{};
        return JsonArrayView(m_owner, m_ref->asArray());
    }

    inline JsonArrayView Json::GetArray() const {
        return JsonArrayView(m_owner, const_cast<::JSON::JsonArray*>(m_ref->asArray()));
    }

} // namespace Terminal
