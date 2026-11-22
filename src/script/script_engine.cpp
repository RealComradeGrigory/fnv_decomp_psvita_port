#include "script_engine.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

extern void logMsg(const char* fmt, ...);

void ScriptEngine::init() {
    m_questStages.clear();
    m_variables.clear();

    // Start introductory quest VCG01: "Ain't That a Kick in the Head"
    setQuestStage("VCG01", (int)QuestStage::WakeUpInBed);
}

void ScriptEngine::setQuestStage(const std::string& questId, int stage) {
    m_questStages[questId] = stage;
    logMsg("ScriptEngine: SetStage %s -> %d", questId.c_str(), stage);

    if (questId == "VCG01") {
        m_activeQuestTitle = "Ain't That a Kick in the Head";
        switch ((QuestStage)stage) {
            case QuestStage::WakeUpInBed:
                m_activeObjective = "Take a seat and talk to Doc Mitchell.";
                break;
            case QuestStage::TalkToDoc:
                m_activeObjective = "Answer Doc Mitchell's questions.";
                break;
            case QuestStage::TakeVigorTest:
                m_activeObjective = "Test your skills on the Vit-o-matic Vigor Tester.";
                break;
            case QuestStage::GetGear:
                m_activeObjective = "Retrieve your weapons and Pip-Boy 3000.";
                break;
            case QuestStage::LeaveClinic:
                m_activeObjective = "Head out the front door into Goodsprings.";
                break;
            case QuestStage::Completed:
                m_activeObjective = "Quest Complete.";
                break;
            default:
                m_activeObjective = "";
                break;
        }
    }
}

int ScriptEngine::getQuestStage(const std::string& questId) const {
    auto it = m_questStages.find(questId);
    if (it != m_questStages.end()) return it->second;
    return 0;
}

bool ScriptEngine::isQuestCompleted(const std::string& questId) const {
    return getQuestStage(questId) >= 100;
}

void ScriptEngine::setVariable(const std::string& name, float value) {
    m_variables[name] = value;
}

float ScriptEngine::getVariable(const std::string& name, float defaultValue) const {
    auto it = m_variables.find(name);
    if (it != m_variables.end()) return it->second;
    return defaultValue;
}

void ScriptEngine::onCellEnter(const char* cellName) {
    logMsg("ScriptEngine: OnCellEnter '%s'", cellName ? cellName : "unknown");
}

void ScriptEngine::onTalkWithNpc(const char* npcId) {
    if (strcmp(npcId, "DocMitchell") == 0) {
        if (getQuestStage("VCG01") == (int)QuestStage::WakeUpInBed) {
            setQuestStage("VCG01", (int)QuestStage::TalkToDoc);
        }
    }
}
