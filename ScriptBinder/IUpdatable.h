#pragma once
#ifndef interface
#define interface struct
#endif

interface IUpdatable
{
   virtual void Update(float deltaTime) = 0;
};
