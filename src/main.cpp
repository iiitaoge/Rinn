#include <iostream>
#include "Core/SparseSet.hpp"
#include "components/Components.hpp"
#include "Core/Registry.hpp"

// �����ӡ����
void print_dense(Rinn::SparseSet<Transform>& set) {
    std::cout << "Dense Array Content:\n";
    for (auto& t : set) { // ������ĵ�����
        std::cout << "[Addr: " << &t << "] Val: (" << t.x << ", " << t.y << ")\n";
    }
    std::cout << "-----------------------\n";
}

int main() {
    // 1. ��ʼ��
    Rinn::SparseSet<Transform> positions(100);

    // 2. ���Ӳ��� (��������)
    std::cout << ">>> Adding Entities 10, 50, 3...\n";
    positions.add(10, { 10.0f, 10.0f });
    positions.add(50, { 50.0f, 50.0f });
    positions.add(3, { 3.0f,  3.0f });

    // 3. ��֤�ڴ�������
    // Ԥ�ڣ�������ַӦ�ý����ţ���� 8 �ֽ� (2��float)
    print_dense(positions);

    // 4. ��ȡ����
    if (positions.has(50)) {
        Transform& t = positions.get(50);
        std::cout << "Entity 50 Position: " << t.x << "\n";
    }

    // 5. ɾ������ (Swap & Pop)
    std::cout << "\n>>> Removing Entity 10 (First element)...\n";
    positions.remove(10);
    // Ԥ�ڣ�Entity 3 ������Ӧ�ñ��ᵽ�� Entity 10 ԭ����λ��

    print_dense(positions);

    // 6. ��֤���������Ƿ��޺���
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