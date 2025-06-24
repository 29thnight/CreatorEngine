#pragma once  
#include <functional>  
#include "BlackBoard.h"  

namespace FSM  
{  
	class FSMState;  // Forward declaration of FSMState class
	class Transition  
	{  
	public:  
		Transition(FSMState* from, FSMState* to, ::ConditionFunc condition)  
			: m_from(from), m_to(to), m_condition(condition) {}  

		bool operator()(const BlackBoard& bb) const  
		{  
			return m_condition(bb);  
		}  

		FSMState* GetFrom() const
		{
			return m_from;
		}

		FSMState* GetTo() const
		{
			return m_to;
		}

	private:  
		FSMState* m_from;  
		FSMState* m_to;  
		::ConditionFunc m_condition;
	};  
}