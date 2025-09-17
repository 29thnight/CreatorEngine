#pragma once
#include <iostream>

// Log ��� �� �ּ� ����
// Log �� �� �ּ� ó��
#define ENABLE_MY_LOG

#ifdef ENABLE_MY_LOG
      // �αװ� ���� ���� ���� ����
#define LOG(...) std::cout << "[" << __func__ << "] " << __VA_ARGS__ << std::endl
#else
    // �αװ� ���� ���� ���� ���� (�ƹ��͵� �� ��)
#define LOG(...) do {} while(0)
#endif