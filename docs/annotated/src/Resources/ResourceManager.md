# ResourceManager.hpp — 纹理资源管理器（深度注释版）

> 文件路径: `src/Resources/ResourceManager.hpp`  
> 角色: 管理游戏纹理的加载、缓存、索引和生命周期。通过整数 ID 索引纹理，避免重复加载。

---

## 文件级设计意图

**问题**: 游戏需要管理 GPU 纹理资源——加载、去重、按 ID 快速访问、最终释放。

**可选方案**:

| 方案 | 去重 | 随机访问 | 内存 | 复杂度 |
|------|------|---------|------|--------|
| 每处 `LoadTexture` | ✗ 可能重复加载 | ✗ | 最简单 | 泄漏风险 |
| **路径→ID 映射 + 数组（当前）** | **✓ 哈希去重** | **O(1) 数组下标** | **线性** | **低** |
| 引用计数（shared_ptr） | ✓ | O(1) 指针 | 略高 | 中 |
| 资产数据库（UUID→资产） | ✓ | O(1) 哈希 | 高 | 高 |

**当前方案的核心思路**: 加载时用 `unordered_map` 去重，存储用 `vector` 取 O(1) 下标访问。组件中只存 `uint16_t` ID（2 字节），而非 8 字节指针。

**缺陷**:
- 不支持引用计数。即使没有实体引用某纹理，它依然占用 GPU 内存直到 `unload_all`。
- 不支持异步加载。`LoadTexture` 是阻塞调用，大量纹理会导致加载画面卡顿。
- 不支持热重载（修改图片后自动刷新）。

---

## 逐行注释

```cpp
class ResourceManager {
    std::vector<Texture2D> textures;
    std::unordered_map<std::string, uint16_t> path_to_id;
```

> **语法知识 — `std::unordered_map<K, V>`**:
>
> 基于哈希表的关联容器。
>
> | 操作 | 平均 | 最坏 |
> |------|------|------|
> | 查找 `find()` | O(1) | O(n) 哈希碰撞 |
> | 插入 `[]` / `insert` | O(1) | O(n) rehash |
> | 删除 `erase` | O(1) | O(n) |
>
> **与 `std::map` 的区别**:
> - `map`: 红黑树，O(log n)，有序，迭代器稳定
> - `unordered_map`: 哈希表，O(1) 平均，无序，rehash 后迭代器失效
>
> **rehash**: 当 `size / bucket_count > load_factor`（默认 1.0）时，哈希表自动扩容并重新散列所有元素。这是 O(n) 操作，但平均分摊后仍是 O(1)。
>
> **std::string 的哈希**: 标准库提供 `std::hash<std::string>` 特化，计算字符串内容的哈希值（通常是 FNV-1a 或类似算法）。

**为什么 `textures` 用 `vector` 而非 `map`？**

组件中存的是 `uint16_t texture_id`。每帧每个精灵都要取纹理，必须是 O(1)。`vector[id]` 是数组下标 = O(1) + 零开销。`map` 查找是 O(log n)。

---

### load_texture — 加载（去重）

```cpp
inline uint16_t ResourceManager::load_texture(const std::string& path) {
    auto it = path_to_id.find(path);
    if (it != path_to_id.end()) {
        return it->second;
    }

    Texture2D tex = LoadTexture(path.c_str());
    uint16_t id = static_cast<uint16_t>(textures.size());
    textures.push_back(tex);
    path_to_id[path] = id;
    return id;
}
```

> **语法知识 — `inline` 在头文件中定义成员函数**:
>
> 类内声明、类外定义的成员函数如果在头文件中，必须加 `inline`。否则多个翻译单元（`.cpp` 文件）包含这个头文件时，会产生重复定义的链接错误。
>
> `inline` 的真正含义不是"内联展开"（编译器自行决定），而是告诉链接器"这个函数可能在多个翻译单元中定义，请只保留一份"。

> **语法知识 — `auto it = path_to_id.find(path)`**:
>
> `find()` 返回迭代器。
> - 找到: `it` 指向 `{key, value}` 对。`it->first` = key, `it->second` = value。
> - 没找到: `it == path_to_id.end()`（past-the-end 迭代器）。
>
> **为什么不用 `path_to_id[path]`？** `operator[]` 在 key 不存在时会**自动插入**一个默认值（`uint16_t()` = 0），导致意外的元素创建。`find()` 只读查找，不修改容器。

> **语法知识 — `path.c_str()`**:
>
> `std::string::c_str()` 返回 `const char*`（以 null 结尾的 C 字符串）。Raylib 是纯 C 库，它的 API 只接受 C 字符串而非 `std::string`。
>
> 返回的指针**只在 string 未被修改时有效**。如果之后修改了 `path`（如 append），指针可能失效。

---

### get_texture

```cpp
inline Texture2D& ResourceManager::get_texture(uint16_t id) {
    assert(id < textures.size() && "Invalid texture ID");
    return textures[id];
}
```

O(1) 数组访问。返回引用让 Raylib 绘制函数直接使用。

---

### 析构函数 — RAII

```cpp
~ResourceManager() {
    if (!textures.empty()) {
        unload_all();
    }
}
```

> **语法知识 — RAII (Resource Acquisition Is Initialization)**:
>
> C++ 的核心资源管理范式：构造时获取资源，析构时释放资源。
>
> **优势**: 即使中途 return 或抛异常，析构函数也一定会被调用（栈展开保证）→ 资源不会泄漏。
>
> **本例**: `ResourceManager` 析构时自动卸载所有 GPU 纹理。不需要手动调用 `unload_all()`。
>
> **注意**: `UnloadTexture` 必须在 Raylib 窗口关闭之前调用。如果 `~ResourceManager()` 在 `CloseWindow()` 之后执行 → 崩溃。`main.cpp` 中的顺序 `res.unload_all()` → `Shutdown()` 避免了这问题。

---

## 文件级总结

| 设计决策 | 选择 | 理由 |
|---------|------|------|
| 去重方式 | unordered_map | O(1) 平均查找 |
| 存储方式 | vector + 整数 ID | O(1) 下标访问，ID 可存入组件 |
| 生命周期 | RAII + 手动 `unload_all` | 自动析构 + 可控时机 |
| ID 类型 | uint16_t | 2 字节，65535 种纹理足够 |
