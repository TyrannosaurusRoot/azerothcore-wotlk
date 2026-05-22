/*
 * Boss Difficulty Addon
 *
 * Makes selected bosses easier by replacing complex mechanics with simple
 * tank-and-spank AI and reducing their maximum health to 50%.
 *
 * Currently covered: High King Maulgar encounter (Gruul's Lair)
 *   NPC 18831 – High King Maulgar
 *   NPC 18832 – Krosh Firehand
 *   NPC 18834 – Olm the Summoner
 *   NPC 18835 – Kiggler the Crazed
 *   NPC 18836 – Blindeye the Seer
 */

#include "AllCreatureScript.h"
#include "Config.h"
#include "ScriptedCreature.h"

// Constants mirrored from gruuls_lair.h / instance_gruuls_lair.cpp
// to avoid a hard dependency on the core script tree.
enum GruulsLairData
{
    GL_DATA_MAULGAR      = 0,
    GL_DATA_ADDS_KILLED  = 10,
    GL_MAX_ADD_NUMBER    = 4,
};

enum GruulsLairNPCs
{
    NPC_MAULGAR            = 18831,
    NPC_KROSH_FIREHAND     = 18832,
    NPC_OLM_THE_SUMMONER   = 18834,
    NPC_KIGGLER_THE_CRAZED = 18835,
    NPC_BLINDEYE_THE_SEER  = 18836,
};

// ---------------------------------------------------------------------------
// Simplified (tank-and-spank) AI for High King Maulgar
// Preserves the loot-gating logic so the encounter progression remains intact:
//   - Loot is withheld until all 4 council members are dead.
//   - Boss state (DONE) and the Gruul door open only when both conditions met.
// ---------------------------------------------------------------------------
struct npc_easy_maulgar : public BossAI
{
    npc_easy_maulgar(Creature* creature) : BossAI(creature, GL_DATA_MAULGAR) { }

    void Reset() override
    {
        _Reset();
        me->SetLootMode(0);
    }

    void JustEngagedWith(Unit* /*who*/) override
    {
        _JustEngagedWith();
    }

    // Called by instance_gruuls_lair::SetData each time a council member dies,
    // with actionId equal to the running kill count (1 … MAX_ADD_NUMBER).
    void DoAction(int32 actionId) override
    {
        if (actionId != GL_MAX_ADD_NUMBER)
            return;

        me->AddLootMode(1);

        if (!me->IsAlive())
        {
            // Maulgar died before the last add – retroactively grant loot.
            me->loot.clear();
            me->loot.FillLoot(me->GetCreatureTemplate()->lootid,
                              LootTemplates_Creature,
                              me->GetLootRecipient(),
                              false, false,
                              me->GetLootMode(), me);
            me->SetDynamicFlag(UNIT_DYNFLAG_LOOTABLE);
            _JustDied();
        }
    }

    void JustDied(Unit* /*killer*/) override
    {
        // Only finalise the encounter once all council members are also down.
        if (instance->GetData(GL_DATA_ADDS_KILLED) == GL_MAX_ADD_NUMBER)
            _JustDied();
    }

    void UpdateAI(uint32 /*diff*/) override
    {
        if (!UpdateVictim())
            return;

        DoMeleeAttackIfReady();
    }
};

// ---------------------------------------------------------------------------
// Simplified (tank-and-spank) AI shared by all four council members.
// Each member reports its death to the instance so the kill counter advances
// and Maulgar's DoAction is called at the right time.
// ---------------------------------------------------------------------------
struct npc_easy_maulgar_council : public ScriptedAI
{
    npc_easy_maulgar_council(Creature* creature) : ScriptedAI(creature)
    {
        instance = creature->GetInstanceScript();
    }

    InstanceScript* instance;

    void Reset() override
    {
        if (instance)
            instance->SetBossState(GL_DATA_MAULGAR, NOT_STARTED);
    }

    void JustEngagedWith(Unit* /*who*/) override
    {
        me->SetInCombatWithZone();
        if (instance)
            instance->SetBossState(GL_DATA_MAULGAR, IN_PROGRESS);
    }

    void JustDied(Unit* /*killer*/) override
    {
        // Increments _addsKilled and calls maulgar->AI()->DoAction(count).
        if (instance)
            instance->SetData(GL_DATA_ADDS_KILLED, 1);
    }

    void UpdateAI(uint32 /*diff*/) override
    {
        if (!UpdateVictim())
            return;

        DoMeleeAttackIfReady();
    }
};

// ---------------------------------------------------------------------------
// AllCreatureScript hook – intercepts AI creation and stat initialisation
// for the five Maulgar encounter NPCs only.
// ---------------------------------------------------------------------------
class BossDifficultyScript : public AllCreatureScript
{
public:
    BossDifficultyScript() : AllCreatureScript("BossDifficultyScript") { }

    // Fired after SelectLevel() sets MaxHealth/Health; halve them to 50%.
    void OnCreatureSelectLevel(const CreatureTemplate* /*cinfo*/, Creature* creature) override
    {
        if (!sConfigMgr->GetOption<bool>("BossDifficulty.Maulgar.ReduceHP", true))
            return;

        if (!IsMaulgarEncounterNPC(creature->GetEntry()))
            return;

        uint32 newHealth = creature->GetMaxHealth() / 2;
        creature->SetCreateHealth(newHealth);
        creature->SetMaxHealth(newHealth);
        creature->SetHealth(newHealth);
    }

    // Returns our simplified AI for the five NPCs; nullptr defers to the
    // registered CreatureScript for every other creature.
    CreatureAI* GetCreatureAI(Creature* creature) const override
    {
        if (!sConfigMgr->GetOption<bool>("BossDifficulty.Maulgar.TankAndSpank", true))
            return nullptr;

        switch (creature->GetEntry())
        {
            case NPC_MAULGAR:
                return new npc_easy_maulgar(creature);
            case NPC_KROSH_FIREHAND:
            case NPC_OLM_THE_SUMMONER:
            case NPC_KIGGLER_THE_CRAZED:
            case NPC_BLINDEYE_THE_SEER:
                return new npc_easy_maulgar_council(creature);
            default:
                return nullptr;
        }
    }

private:
    static bool IsMaulgarEncounterNPC(uint32 entry)
    {
        switch (entry)
        {
            case NPC_MAULGAR:
            case NPC_KROSH_FIREHAND:
            case NPC_OLM_THE_SUMMONER:
            case NPC_KIGGLER_THE_CRAZED:
            case NPC_BLINDEYE_THE_SEER:
                return true;
            default:
                return false;
        }
    }
};

void Addmod_boss_difficultyScripts()
{
    new BossDifficultyScript();
}
