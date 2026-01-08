#include <iostream>
#include "core/SparseSet.hpp"
#include "components/Components.hpp"

// 方便打印调试
void print_dense(Rinn::SparseSet<Transform>& set) {
    std::cout << "Dense Array Content:\n";
    for (auto& t : set) { // 测试你的迭代器
        std::cout << "[Addr: " << &t << "] Val: (" << t.x << ", " << t.y << ")\n";
    }
    std::cout << "-----------------------\n";
}

int main() {
    // 1. 初始化
    Rinn::SparseSet<Transform> positions(100);

    // 2. 添加测试 (乱序添加)
    std::cout << ">>> Adding Entities 10, 50, 3...\n";
    positions.add(10, { 10.0f, 10.0f });
    positions.add(50, { 50.0f, 50.0f });
    positions.add(3, { 3.0f,  3.0f });

    // 3. 验证内存连续性
    // 预期：三个地址应该紧挨着，相差 8 字节 (2个float)
    print_dense(positions);

    // 4. 获取测试
    if (positions.has(50)) {
        Transform& t = positions.get(50);
        std::cout << "Entity 50 Position: " << t.x << "\n";
    }

    // 5. 删除测试 (Swap & Pop)
    std::cout << "\n>>> Removing Entity 10 (First element)...\n";
    positions.remove(10);
    // 预期：Entity 3 的数据应该被搬到了 Entity 10 原来的位置

    print_dense(positions);

    // 6. 验证反向索引是否修好了
    if (positions.has(3)) {
        std::cout << "Entity 3 is still alive.\n";
    }
    else {
        std::cout << "ERROR: Entity 3 vanished!\n";
    }

    if (!positions.has(10)) {
        std::cout << "Entity 10 is correctly removed.\n";
    }

    return 0;
}