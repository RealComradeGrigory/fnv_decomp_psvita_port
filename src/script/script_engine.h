#pragma once

#include <string>
#include <unordered_map>
#include <vector>

// Quest Stages for "Ain't That a Kick in the Head" (VCG01)
enum class QuestStage {
    Unstarted = 0,
    WakeUpInBed = 10,       // "Take a seat across from Doc Mitchell"
    TalkToDoc = 20,         // "Answer Doc Mitchell's questions"
    TakeVigorTest = 30,     // "Test your strength on the Vit-o-matic"
    GetGear = 40,           // "Take your gear from Doc Mitchell"
    LeaveClinic = 50,       // "Leave Doc Mitchell's house"
    Completed = 100
};

class ScriptEngine {
public:
    ScriptEngine() = default;
    ~ScriptEngine() = default;

    void init();

    // Quest Management
    void setQuestStage(const std::string& questId, int stage);
    int getQuestStage(const std::string& questId) const;
    bool isQuestCompleted(const std::string& questId) const;

    // Script Variables (e.g. "DoOnce", "PlayerLeft")
    void setVariable(const std::string& name, float value);
    float getVariable(const std::string& name, float defaultValue = 0.0f) const;

    // Active Quest Objective text
    const char* getActiveQuestTitle() const { return m_activeQuestTitle.c_str(); }
    const char* getActiveObjective() const { return m_activeObjective.c_str(); }

    // Trigger events
    void onCellEnter(const char* cellName);
    void onTalkWithNpc(const char* npcId);

private:
    std::unordered_map<std::string, int> m_questStages;
    std::unordered_map<std::string, float> m_variables;

    std::string m_activeQuestTitle;
    std::string m_activeObjective;
};
