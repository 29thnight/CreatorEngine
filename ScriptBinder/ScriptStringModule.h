#pragma once
#include <string_view>

#pragma region Script File String
constexpr std::string_view scriptIncludeString
{
	"#pragma once\n"
	"#include \"Core.Minimal.h\"\n"
	"#include \"ModuleBehavior.h\"\n"
	"\n"
	"class "
};

constexpr std::string_view scriptInheritString
{
	" : public ModuleBehavior\n"
	"{\n"
	"public:\n"
	"	MODULE_BEHAVIOR_BODY("
};

constexpr std::string_view scriptBodyString
{
	")\n"
	"	virtual void Awake() override {}\n"
	"	virtual void Start() override;\n"
	"	virtual void FixedUpdate(float fixedTick) override {}\n"
	"	virtual void OnTriggerEnter(const Collision& collision) override {}\n"
	"	virtual void OnTriggerStay(const Collision& collision) override {}\n"
	"	virtual void OnTriggerExit(const Collision& collision) override {}\n"
	"	virtual void OnCollisionEnter(const Collision& collision) override {}\n"
	"	virtual void OnCollisionStay(const Collision& collision) override {}\n"
	"	virtual void OnCollisionExit(const Collision& collision) override {}\n"
	"	virtual void Update(float tick) override;\n"
	"	virtual void LateUpdate(float tick) override {}\n"
	"	virtual void OnDisable() override  {}\n"
	"	virtual void OnDestroy() override  {}\n"
};

constexpr std::string_view scriptEndString
{
	"};\n"
};

constexpr std::string_view scriptCppString
{
	"#include \""
};

constexpr std::string_view scriptCppEndString
{
	".h\"\n"
	"#include \"pch.h\""
	"\n"
	"void "
};

constexpr std::string_view scriptCppEndBodyString
{
	"::Start()\n"
	"{\n"
	"}\n"
	"\n"
	"void "
};

constexpr std::string_view scriptCppEndUpdateString
{
	"::Update(float tick)\n"
	"{\n"
	"}\n"
	"\n"
};

constexpr std::string_view scriptFactoryIncludeString
{
	"#include \""
};

constexpr std::string_view scriptFactoryFunctionString
{
	"		CreateFactory::GetInstance()->RegisterFactory(\""
};

constexpr std::string_view scriptFactoryFunctionLambdaString
{
	"\", []() { return new "
};

constexpr std::string_view scriptFactoryFunctionEndString
{
	"(); });\n"
};

constexpr std::string_view markerFactoryHeaderString
{
	"// Automation include ScriptClass header"
};

constexpr std::string_view markerFactoryFuncString
{
	"// Register the factory function for TestBehavior Automation"
};
#pragma endregion

#pragma region Action and Condition Node String
constexpr std::string_view actionNodeIncludeString
{
	"#include \"Core.Minimal.h\"\n"
	"#include \"BTHeader.h\"\n"
	"\n"
	"using namespace BT;\n"
	"\n"
	"class "
};

constexpr std::string_view actionNodeInheritString
{
	" : public ActionNode\n"
	"{\n"
	"public:\n"
	"	BT_ACTION_BODY("
};

constexpr std::string_view actionNodeEndString
{
	")\n"
	"	virtual NodeStatus Tick(float deltatime, BlackBoard& blackBoard) override;\n"
	"};\n"
};

constexpr std::string_view actionNodeCPPString
{
	"#include \""
};

constexpr std::string_view actionNodeCPPEndString
{
	".h\"\n"
	"#include \"pch.h\"\n"
	"\n"
	"NodeStatus "
};

constexpr std::string_view actionNodeCPPEndBodyString
{
	"::Tick(float deltatime, BlackBoard& blackBoard)\n"
	"{\n"
	"	return NodeStatus::Success;\n"
	"}\n"
};

constexpr std::string_view conditionNodeIncludeString
{
	"#include \"Core.Minimal.h\"\n"
	"#include \"BTHeader.h\"\n"
	"\n"
	"using namespace BT;\n"
	"\n"
	"class "
};

constexpr std::string_view conditionNodeInheritString
{
	" : public ConditionNode\n"
	"{\n"
	"public:\n"
	"	BT_CONDITION_BODY("
};

constexpr std::string_view conditionNodeEndString
{
	")\n"
	"	virtual bool ConditionCheck(float deltatime, const BlackBoard& blackBoard) override;\n"
	"};\n"
};

constexpr std::string_view conditionNodeCPPString
{
	"#include \""
};

constexpr std::string_view conditionNodeCPPEndString
{
	".h\"\n"
	"#include \"pch.h\"\n"
	"\n"
	"bool "
};

constexpr std::string_view conditionNodeCPPEndBodyString
{
	"::ConditionCheck(float deltatime, const BlackBoard& blackBoard)\n"
	"{\n"
	"	return false;\n"
	"}\n"
};

constexpr std::string_view conditionDecoratorNodeIncludeString
{
	"#include \"Core.Minimal.h\"\n"
	"#include \"BTHeader.h\"\n"
	"\n"
	"using namespace BT;\n"
	"\n"
	"class "
};

constexpr std::string_view conditionDecoratorNodeInheritString
{
	" : public ConditionDecoratorNode\n"
	"{\n"
	"public:\n"
	"	BT_CONDITIONDECORATOR_BODY("
};

constexpr std::string_view conditionDecoratorNodeEndString
{
	")\n"
	"	virtual bool ConditionCheck(float deltatime, const BlackBoard& blackBoard) override;\n"
	"};\n"
};

constexpr std::string_view conditionDecoratorNodeCPPString
{
	"#include \""
};

constexpr std::string_view conditionDecoratorNodeCPPEndString
{
	".h\"\n"
	"#include \"pch.h\"\n"
	"\n"
	"bool "
};

constexpr std::string_view conditionDecoratorNodeCPPEndBodyString
{
	"::ConditionCheck(float deltatime, const BlackBoard& blackBoard)\n"
	"{\n"
	"	return false;\n"
	"}\n"
};

constexpr std::string_view actionFactoryIncludeString
{
	"#include \""
};

constexpr std::string_view actionFactoryFunctionString
{
	"		ActionCreateFactory::GetInstance()->RegisterFactory(\""
};

constexpr std::string_view actionFactoryFunctionLambdaString
{
	"\", []() { return new "
};

constexpr std::string_view actionFactoryFunctionEndString
{
	"(); });\n"
};

constexpr std::string_view conditionFactoryIncludeString
{
	"#include \""
};

constexpr std::string_view conditionFactoryFunctionString
{
	"		ConditionCreateFactory::GetInstance()->RegisterFactory(\""
};

constexpr std::string_view conditionFactoryFunctionLambdaString
{
	"\", []() { return new "
};

constexpr std::string_view conditionFactoryFunctionEndString
{
	"(); });\n"
};

constexpr std::string_view conditionDecoratorFactoryIncludeString
{
	"#include \""
};

constexpr std::string_view conditionDecoratorFactoryFunctionString
{
	"		ConditionDecoratorCreateFactory::GetInstance()->RegisterFactory(\""
};

constexpr std::string_view conditionDecoratorFactoryFunctionLambdaString
{
	"\", []() { return new "
};

constexpr std::string_view conditionDecoratorFactoryFunctionEndString
{
	"(); });\n"
};

constexpr std::string_view markerActionFactoryHeaderString
{
	"// Automation include ActionNodeClass header"
};

constexpr std::string_view markerActionFactoryFuncString
{
	"// Register the factory function for BTAction Automation"
};

constexpr std::string_view markerConditionFactoryHeaderString
{
	"// Automation include ConditionNodeClass header"
};

constexpr std::string_view markerConditionFactoryFuncString
{
	"// Register the factory function for BTCondition Automation"
};

constexpr std::string_view markerConditionDecoratorFactoryHeaderString
{
	"// Automation include ConditionDecoratorNodeClass header"
};

constexpr std::string_view markerConditionDecoratorFactoryFuncString
{
	"// Register the factory function for BTConditionDecorator Automation"
};

#pragma endregion

#pragma region AnimationFSM Scirpt String
constexpr std::string_view AniBehaviorIncludeString
{
	"#include \"Core.Minimal.h\"\n"
	"#include \"AniBehavior.h\"\n"
	"\n"
	"class "
};
constexpr std::string_view AniBehaviorInheritString
{
	" : public AniBehavior\n"
	"{\n"
	"public:\n"
	"	ANIBEHAVIOR_BODY("
};
constexpr std::string_view AniBehaviorEndString
{
	")\n"
	"	virtual void Enter() override;\n"
	"	virtual void Update(float tick) override;\n"
	"	virtual void Exit() override;\n"
	"};\n"
};
constexpr std::string_view AniBehaviorCPPString
{
	"#include \""
};
constexpr std::string_view AniBehaviorCPPEndString
{
	".h\"\n"
	"#include \"pch.h\"\n"
};
constexpr std::string_view AniBehaviorCPPEndEnterBodyString
{
	"::Enter()\n"
	"{\n"
	"}\n\n"
};

constexpr std::string_view AniBehaviorCPPEndUpdateString
{
	"::Update(float deltaTime)\n"
	"{\n"
	"}\n\n"
};
constexpr std::string_view AniBehaviorCPPEndExitBodyString
{
	"::Exit()\n"
	"{\n"
	"}\n"
};

constexpr std::string_view AniBehaviorFactoryIncludeString
{
	"#include \""
};

constexpr std::string_view AniBehaviorFactoryFunctionString
{
	"		AniBehaviorFactory::GetInstance()->RegisterFactory(\""
};

constexpr std::string_view AniBehaviorFactoryFunctionLambdaString
{
	"\", []() { return new "
};

constexpr std::string_view AniBehaviorFactoryFunctionEndString
{
	"(); });\n"
};

constexpr std::string_view AniBehaviorMarkerFactoryHeaderString
{
	"// Automation include AniScriptClass header"
};

constexpr std::string_view AniBehaviorMarkerFactoryFuncString
{
	"// Register the factory function for AniBehavior Automation"
};
#pragma endregion