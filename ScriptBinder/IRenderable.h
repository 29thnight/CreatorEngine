#pragma once
#ifndef interface
#define interface struct
#endif

//TODO : 컬링 기본 객체로 변경할 예정
interface 
[[deprecated("Will be change IRenderable interface soon, Please! Don't inheritance this interface")]] 
IRenderable
{
	[[deprecated("Will be change IRenderable interface soon, Please! Don't inheritance this interface")]]
	virtual bool IsEnabled() const = 0;
	[[deprecated("Will be change IRenderable interface soon, Please! Don't inheritance this interface")]]
	virtual void SetEnabled(bool enabled) = 0;
};