/* Copyright (C) 2015-2022, Wazuh Inc.
 * All rights reserved.
 *
 * This program is free software; you can redistribute it
 * and/or modify it under the terms of the GNU General Public
 * License (version 2) as published by the FSF - Free Software
 * Foundation.
 */
#include "opBuilderSCAdecoder.hpp"

#include <algorithm>
#include <map>
#include <optional>
#include <string>
#include <tuple>
#include <variant>

#include "syntax.hpp"

#include <baseHelper.hpp>
#include <baseTypes.hpp>
#include <logging/logging.hpp>
#include <utils/stringUtils.hpp>
#include <wdb/wdb.hpp>

namespace builder::internals::builders
{

namespace sca
{
constexpr auto TYPE_CHECK = "check";       ///< Check Event type
constexpr auto TYPE_SUMMARY = "summary";   ///< Scan info Event type
constexpr auto TYPE_POLICIES = "policies"; ///< Policies Event type
constexpr auto TYPE_DUMP_END = "dump_end"; ///< Dump end Event type

namespace field
{

/**
 * @brief operator ++, for the enum class Field
 *
 * @param field Field to increment
 * @return Name& next field
 */
Name& operator++(Name& field)
{
    if (Name::A_END == field)
    {
        throw std::out_of_range("For Name& operator ++)");
    }
    field = Name(static_cast<std::underlying_type<Name>::type>(field) + 1);
    return field;
}

/**
 * @brief Type of field
 */
enum class Type
{
    STRING,
    INT,
    BOOL,
    ARRAY,
    OBJECT
};

std::string getRealtivePath(Name field)
{
    switch (field)
    {
        case Name::ROOT: return "";
        case Name::ID: return "/id";
        case Name::SCAN_ID: return "/scan_id";
        case Name::DESCRIPTION: return "/description";
        case Name::REFERENCES: return "/references";
        case Name::START_TIME: return "/start_time";
        case Name::END_TIME: return "/end_time";
        case Name::PASSED: return "/passed";
        case Name::FAILED: return "/failed";
        case Name::INVALID: return "/invalid";
        case Name::TOTAL_CHECKS: return "/total_checks";
        case Name::SCORE: return "/score";
        case Name::HASH: return "/hash";
        case Name::HASH_FILE: return "/hash_file";
        case Name::FILE: return "/file";
        case Name::NAME: return "/name";
        case Name::FIRST_SCAN: return "/first_scan";
        case Name::FORCE_ALERT: return "/force_alert";
        case Name::POLICY: return "/policy";
        case Name::POLICY_ID: return "/policy_id";
        case Name::POLICIES: return "/policies";
        case Name::CHECK: return "/check";
        case Name::CHECK_ID: return "/check/id";
        case Name::CHECK_TITLE: return "/check/title";
        case Name::CHECK_DESCRIPTION: return "/check/description";
        case Name::CHECK_RATIONALE: return "/check/rationale";
        case Name::CHECK_REMEDIATION: return "/check/remediation";
        case Name::CHECK_REFERENCES: return "/check/references";
        case Name::CHECK_COMPLIANCE: return "/check/compliance";
        case Name::CHECK_CONDITION: return "/check/condition";
        case Name::CHECK_DIRECTORY: return "/check/directory";
        case Name::CHECK_PROCESS: return "/check/process";
        case Name::CHECK_REGISTRY: return "/check/registry";
        case Name::CHECK_COMMAND: return "/check/command";
        case Name::CHECK_RULES: return "/check/rules";
        case Name::CHECK_STATUS: return "/check/status";
        case Name::CHECK_REASON: return "/check/reason";
        case Name::CHECK_RESULT: return "/check/result";
        case Name::CHECK_FILE: return "/check/file";
        case Name::ELEMENTS_SENT: return "/elements_sent";
        case Name::TYPE: return "/type";
        case Name::CHECK_PREVIOUS_RESULT: return "/check/previous_result";
        default:
            throw std::logic_error(
                "getRealtivePath: unknown field: "
                + std::to_string(static_cast<std::underlying_type<Name>::type>(field)));
    }
}

/**
 * @brief Copy field from original event to sca event if exist.
 *
 * @param ctx The decoder context, decode info status.
 * @param field Field to copy
 */
inline void copyIfExist(const DecodeCxt& ctx, Name field)
{
    const auto origin = ctx.sourcePath.at(field);
    if (ctx.event->exists(origin))
    {
        ctx.event->set(ctx.destinationPath.at(field), origin);
    }
};

/**
 * @brief Transform a field from original event to array in sca event if exist.
 *
 * Transform a string field with a list of values separated by ',' to an array
 * of strings and set it in the sca event.
 * @param ctx The decoder context, decode info status.
 * @param field Field to transform.
 */
inline void csvStr2ArrayIfExist(const DecodeCxt& ctx, Name field)
{

    const auto csv = ctx.getSrcStr(field);
    if (csv)
    {
        const auto scaArrayPath = ctx.destinationPath.at(field);
        const auto cvaArray = utils::string::split(csv.value().c_str(), ',');

        ctx.event->setArray(scaArrayPath);
        for (const auto& csvItem : cvaArray)
        {
            ctx.event->appendString(csvItem, scaArrayPath);
        }
    }
};

/**
 * @brief Represents a condition to check.
 *
 * field::name is the the field to check.
 * field::Type is the type of the field to check.
 * bool if is true, then the field is mandatory.
 */
using conditionToCheck = std::tuple<field::Name, field::Type, bool>;

/**
 * @brief Check array of conditions against a given event.
 *
 * Check the types of fields and if they are present when they are mandatory.
 * The conditions are checked against the event in the order they are given.
 * If any condition is not met, the event is not valid and returns false.
 * @param ctx The decoder context, decode info status.
 * @param conditions The array of conditions to check against the event.
 * @return true If all conditions are met.
 * @return false If any condition is not met.
 */
inline bool isValidEvent(const DecodeCxt& ctx,
                         const std::vector<conditionToCheck>& conditions)
{
    // Check types and mandatory fields if is necessary. Return false on fail.
    const auto isValidCondition =
        [&ctx](field::Type type, const std::string& path, bool mandatory)
    {
        if (ctx.event->exists(path))
        {
            switch (type)
            {
                case field::Type::STRING: return ctx.event->isString(path);
                case field::Type::INT: return ctx.event->isInt(path);
                case field::Type::BOOL: return ctx.event->isBool(path);
                case field::Type::ARRAY: return ctx.event->isArray(path);
                case field::Type::OBJECT: return ctx.event->isObject(path);
                default: return false;
            }
        }
        else if (mandatory)
        {
            return false;
        }
        return true;
    };

    for (const auto& [field, type, mandatory] : conditions)
    {
        const auto path = ctx.sourcePath.at(field);
        if (!isValidCondition(type, path, mandatory))
        {
            return false; // Some condition is not met.
        }
    }

    return true;
};

} // namespace field

/**
 * @brief Get the Rule String from de code rule.
 *
 * @param ruleChar The code rule.
 * @return std::optional<std::string> The rule string.
 */
inline std::optional<std::string> getRuleTypeStr(const char ruleChar)
{
    switch (ruleChar)
    {
        case 'f': return "file";
        case 'd': return "directory";
        case 'r': return "registry";
        case 'c': return "command";
        case 'p': return "process";
        case 'n': return "numeric";
        default: return {};
    }
};

/**
 * @brief Perform a query on the database.
 *
 * Perform a query on wdb and expect a result like:
 * - "not found"
 * - "found ${utilPayload}"
 * @param query The query to perform.
 * @param wdb The database to query.
 * @param payload parse paryload after found.
 * @return <SearchResult::FOUND, ${utilPayload}> if "found XXX" result received.
 * @return <SearchResult::NOT_FOUND, ""> if "not found" result received.
 * @return <SearchResult::ERROR, ""> otherwise.
 */
std::tuple<SearchResult, std::string> searchAndParse(
    const std::string& query, std::shared_ptr<wazuhdb::WazuhDB> wdb, bool parse = true)
{
    std::string retStr {};
    SearchResult retCode {SearchResult::ERROR};

    const auto [rescode, payload] = wdb->tryQueryAndParseResult(query);

    if (wazuhdb::QueryResultCodes::OK == rescode && payload.has_value())
    {
        if (utils::string::startsWith(payload.value(), "found"))
        {
            retCode = SearchResult::FOUND;
            try
            {
                if (parse)
                {
                    retStr = payload.value().substr(6); // Remove "found "
                }
                else
                {
                    retStr = {};
                }
            }
            catch (const std::out_of_range& e)
            {
                WAZUH_LOG_WARN("[SCA] Error parsing result: '{}', cannot remove 'found ' "
                               "of query: '{}'",
                               payload.value(),
                               query);
                retCode = SearchResult::ERROR;
            }
        }
        else if (utils::string::startsWith(payload.value(), "not found"))
        {
            retCode = SearchResult::NOT_FOUND;
        }
    }

    return {retCode, retStr};
};

/****************************************************************************************
                                 Check Event info (type 'check')
*****************************************************************************************/

bool isValidCheckEvent(const DecodeCxt& ctx)
{
    auto retval {true};

    // CheckEvent conditions list
    const std::vector<field::conditionToCheck> listFieldConditions = {
        {field::Name::CHECK_COMMAND, field::Type::STRING, false},
        {field::Name::CHECK_COMPLIANCE, field::Type::OBJECT, false},
        {field::Name::CHECK_CONDITION, field::Type::STRING, false},
        {field::Name::CHECK_DESCRIPTION, field::Type::STRING, false},
        {field::Name::CHECK_DIRECTORY, field::Type::STRING, false},
        {field::Name::CHECK_FILE, field::Type::STRING, false},
        {field::Name::CHECK_ID, field::Type::INT, true},
        {field::Name::CHECK_PROCESS, field::Type::STRING, false},
        {field::Name::CHECK_RATIONALE, field::Type::STRING, false},
        {field::Name::CHECK_REASON, field::Type::STRING, false},
        {field::Name::CHECK_REFERENCES, field::Type::STRING, false},
        {field::Name::CHECK_REGISTRY, field::Type::STRING, false},
        {field::Name::CHECK_REMEDIATION, field::Type::STRING, false},
        {field::Name::CHECK_RESULT, field::Type::STRING, false},
        {field::Name::CHECK_RULES, field::Type::ARRAY, false},
        {field::Name::CHECK_TITLE, field::Type::STRING, true},
        {field::Name::CHECK, field::Type::OBJECT, true},
        {field::Name::ID, field::Type::INT, true},
        {field::Name::POLICY_ID, field::Type::STRING, true},
        {field::Name::POLICY, field::Type::STRING, true}};

    if (!field::isValidEvent(ctx, listFieldConditions))
    {
        retval = false;
    }
    else
    {
        /*
            If `result` does not exist then `status` must exists.
            If `status` exists then `reason` must exists as well.
        */
        const bool existResult = ctx.existsSrc(field::Name::CHECK_RESULT);
        const bool existReason = ctx.existsSrc(field::Name::CHECK_REASON);
        const bool existStatus = ctx.existsSrc(field::Name::CHECK_STATUS);

        if ((!existResult && !existStatus) || (existStatus && !existReason))
        {
            retval = false;
        }
    }

    return retval;
}

void fillCheckEvent(const DecodeCxt& ctx, const std::string& previousResult)
{

    ctx.event->setString("check", ctx.destinationPath.at(field::Name::TYPE));

    // Save the previous result
    if (!previousResult.empty())
    {
        ctx.event->setString(previousResult.c_str(),
                             ctx.destinationPath.at(field::Name::CHECK_PREVIOUS_RESULT));
    }

    field::copyIfExist(ctx, field::Name::ID);
    field::copyIfExist(ctx, field::Name::POLICY);
    field::copyIfExist(ctx, field::Name::POLICY_ID);

    field::copyIfExist(ctx, field::Name::CHECK_ID);
    field::copyIfExist(ctx, field::Name::CHECK_TITLE);
    field::copyIfExist(ctx, field::Name::CHECK_DESCRIPTION);
    field::copyIfExist(ctx, field::Name::CHECK_RATIONALE);
    field::copyIfExist(ctx, field::Name::CHECK_REMEDIATION);
    field::copyIfExist(ctx, field::Name::CHECK_COMPLIANCE);
    field::copyIfExist(ctx, field::Name::CHECK_REFERENCES);

    // Optional fields with csv
    field::csvStr2ArrayIfExist(ctx, field::Name::CHECK_FILE);
    field::csvStr2ArrayIfExist(ctx, field::Name::CHECK_DIRECTORY);
    field::csvStr2ArrayIfExist(ctx, field::Name::CHECK_REGISTRY);
    field::csvStr2ArrayIfExist(ctx, field::Name::CHECK_PROCESS);
    field::csvStr2ArrayIfExist(ctx, field::Name::CHECK_COMMAND);

    if (ctx.existsSrc(field::Name::CHECK_RESULT))
    {
        ctx.event->set(ctx.destinationPath.at(field::Name::CHECK_RESULT),
                       ctx.sourcePath.at(field::Name::CHECK_RESULT));
    }
    else
    {
        field::copyIfExist(ctx, field::Name::CHECK_STATUS);
        field::copyIfExist(ctx, field::Name::CHECK_REASON);
    }
}

void insertCompliance(const DecodeCxt& ctx, const int checkID)
{
    const auto& compliance = ctx.getSrcObject(field::Name::CHECK_COMPLIANCE);

    if (!compliance.has_value())
    {
        return;
    }

    for (const auto& [key, jsonValue] : compliance.value())
    {
        const auto value = jsonValue.getString();
        if (!value.has_value())
        {
            WAZUH_LOG_WARN("[SCA] Expected string for compliance item '{}'",
                           jsonValue.str());
            continue;
        }
        // Saving compliance fields to database for event id
        const auto query = fmt::format("agent {} sca insert_compliance {}|{}|{}",
                                       ctx.agentID,
                                       checkID,
                                       key,
                                       value.value());

        const auto [res, payload] = ctx.wdb->tryQueryAndParseResult(query);
        if (wazuhdb::QueryResultCodes::OK != res)
        {
            WAZUH_LOG_WARN("[SCA] Failed to insert compliance '{}' for check '{}'",
                           value.value(),
                           checkID);
        }
    }
}

void insertRules(const DecodeCxt& ctx, const int checkID)
{
    // Save rules
    const auto rules = ctx.getSrcArray(field::Name::CHECK_RULES);

    if (!rules.has_value())
    {
        return;
    }

    for (const auto& jsonRule : rules.value())
    {

        const auto rule = jsonRule.getString();
        if (!rule.has_value())
        {
            WAZUH_LOG_WARN("[SCA] Expected string for rule '{}'", jsonRule.str());
            continue;
        }

        const auto type = getRuleTypeStr(rule.value()[0]);
        if (type)
        {
            const auto query = fmt::format("agent {} sca insert_rules {}|{}|{}",
                                           ctx.agentID,
                                           checkID,
                                           type.value(),
                                           rule.value());

            const auto [res, payload] = ctx.wdb->tryQueryAndParseResult(query);
            if (wazuhdb::QueryResultCodes::OK != res)
            {
                WAZUH_LOG_WARN("[SCA] Failed to insert rule '{}' for check '{}'",
                               rule.value(),
                               checkID);
            }
        }
        else
        {
            WAZUH_LOG_WARN("[SCA] Invalid rule type '{}'", rule.value());
        }
    }
}

std::optional<std::string> handleCheckEvent(const DecodeCxt& ctx)
{

    // Check types of fields and if they are mandatory
    if (!isValidCheckEvent(ctx))
    {
        // TODO: Check this message. exit error
        WAZUH_LOG_WARN("[SCA] Invalid check event, discarding..");
        return "Invalid check event,";
    }

    // Get the necesary fields for the query
    const auto checkID = ctx.getSrcInt(field::Name::CHECK_ID).value();
    const auto result = ctx.getSrcStr(field::Name::CHECK_RESULT).value_or("");
    const auto status = ctx.getSrcStr(field::Name::CHECK_STATUS).value_or("");
    const auto reason = ctx.getSrcStr(field::Name::CHECK_REASON).value_or("");

    // Prepare and execute the policy monitoring
    const auto scaQuery = fmt::format("agent {} sca query {}", ctx.agentID, checkID);
    const auto [resPreviosResult, previousResult] = searchAndParse(scaQuery, ctx.wdb);

    // Generate the new query to save or update the policy monitoring
    std::string saveQuery {};
    switch (resPreviosResult)
    {
        case SearchResult::FOUND:
        {
            // There is a previous result, update it
            const auto id = ctx.getSrcInt(field::Name::ID).value_or(-1);

            saveQuery = fmt::format("agent {} sca update {}|{}|{}|{}|{}",
                                    ctx.agentID,
                                    checkID,
                                    result,
                                    status,
                                    reason,
                                    id);
            break;
        }
        case SearchResult::NOT_FOUND:
        {
            // There is no previous result, save it
            const auto rootPath = ctx.sourcePath.at(field::Name::ROOT);
            const auto root = ctx.event->str(rootPath).value_or("{}");

            saveQuery = fmt::format("agent {} sca insert {}", ctx.agentID, root);

            break;
        }
        case SearchResult::ERROR:
        default:
            // if query fails, no sense to continue
            WAZUH_LOG_WARN(
                "[SCA] Error querying policy monitoring database for agent '{}'",
                ctx.agentID);
            return "Error querying policy monitoring database for agent '{}";
    }
    // Save or update the policy monitoring
    const auto [resSavePolicy, empty] = ctx.wdb->tryQueryAndParseResult(saveQuery);
    if (wazuhdb::QueryResultCodes::OK != resSavePolicy)
    {
        WAZUH_LOG_WARN("Error saving policy monitoring for agent '{}'", ctx.agentID);
    }

    // If policies are new, then save the rules and compliance
    if (SearchResult::NOT_FOUND == resPreviosResult)
    {
        insertCompliance(ctx, checkID);
        insertRules(ctx, checkID);
    }

    // Normalize the SCA event and add the previous result if exists
    const bool normalize = result.empty()
                               ? (!status.empty() && (previousResult != status))
                               : (previousResult != result);

    if (normalize)
    {
        fillCheckEvent(ctx, previousResult);
    }

    return std::nullopt; // Success
}

/****************************************************************************************
                                END Check Event info
                                SCAN Info (type Summary)
*****************************************************************************************/

bool isValidScanInfoEvent(const DecodeCxt& ctx)
{

    // ScanInfo conditions list
    const std::vector<field::conditionToCheck> conditions = {
        {field::Name::POLICY_ID, field::Type::STRING, true},
        {field::Name::SCAN_ID, field::Type::INT, true},
        {field::Name::START_TIME, field::Type::INT, true},
        {field::Name::END_TIME, field::Type::INT, true},
        {field::Name::PASSED, field::Type::INT, true},
        {field::Name::FAILED, field::Type::INT, true},
        {field::Name::INVALID, field::Type::INT, true},
        {field::Name::TOTAL_CHECKS, field::Type::INT, true},
        {field::Name::SCORE, field::Type::INT, true},
        {field::Name::HASH, field::Type::STRING, true},
        {field::Name::HASH_FILE, field::Type::STRING, true},
        {field::Name::FILE, field::Type::STRING, true},
        {field::Name::DESCRIPTION, field::Type::STRING, false},
        {field::Name::REFERENCES, field::Type::STRING, false},
        {field::Name::NAME, field::Type::STRING, true},
        /*
       To force the alert, the SCA module sends the `/force_alert` field
       (field::Name::FORCE_ALERT) with the string value "1". Otherwise it does not send
       the field, so the content of the field is not important.
       In the case of the '/first_scan' (field::Name::FIRST_SCAN), the field is sent with
       the integer 1, and uses the same logic as for forcing the alert.
        */
    };

    return field::isValidEvent(ctx, conditions);
}

void pushDumpRequest(const DecodeCxt& ctx, const std::string& policyId, bool firstScan)
{

    if (!ctx.forwarderSocket->isConnected())
    {
        try
        {
            ctx.forwarderSocket->socketConnect();
        }
        catch (const std::exception& e)
        {
            WAZUH_LOG_WARN("[SCA] Error connecting to forwarder socket: {}", e.what());
            return;
        }
    }

    const auto msg =
        fmt::format("{}:sca-dump:{}:{}", ctx.agentID, policyId, firstScan ? "1" : "0");

    // Send the message to the forwarder, can fail but no throw exception,
    // becouse it is connected to the forwarder socket
    const auto sendStatus = ctx.forwarderSocket->sendMsg(msg);
    switch (sendStatus)
    {
        case base::utils::socketInterface::SendRetval::SUCCESS: break;
        case base::utils::socketInterface::SendRetval::SIZE_TOO_LONG:
            WAZUH_LOG_WARN(
                "[SCA] Error sending message to forwarder: message too long: {}", msg);
            break;
        case base::utils::socketInterface::SendRetval::SOCKET_ERROR:
        default:
            WAZUH_LOG_WARN("[SCA] Error database dump request for agent '{}'. {} ({})",
                           ctx.agentID,
                           strerror(errno),
                           errno);
            ctx.forwarderSocket->socketDisconnect(); // Try to reconnect the next time
            break;
    }

    return;
}

bool SaveScanInfo(const DecodeCxt& ctx, bool update)
{
    // "Retrieving sha256 hash for policy id: policy_id"
    std::string query {};

    // All mandatory fields are present
    const auto pmStartScan = ctx.getSrcInt(field::Name::START_TIME).value();
    const auto pmEndScan = ctx.getSrcInt(field::Name::END_TIME).value();
    const auto scanID = ctx.getSrcInt(field::Name::SCAN_ID).value();
    const auto pass = ctx.getSrcInt(field::Name::PASSED).value();
    const auto failed = ctx.getSrcInt(field::Name::FAILED).value();
    const auto invalid = ctx.getSrcInt(field::Name::INVALID).value();
    const auto totalChecks = ctx.getSrcInt(field::Name::TOTAL_CHECKS).value();
    const auto score = ctx.getSrcInt(field::Name::SCORE).value();
    const auto hash = ctx.getSrcStr(field::Name::HASH).value();
    const auto policyID = ctx.getSrcStr(field::Name::POLICY_ID).value();

    if (update)
    {
        query = fmt::format(
            "agent {} sca update_scan_info_start {}|{}|{}|{}|{}|{}|{}|{}|{}|{}",
            ctx.agentID,
            policyID,
            pmStartScan,
            pmEndScan,
            scanID,
            pass,
            failed,
            invalid,
            totalChecks,
            score,
            hash);
    }
    else
    {
        query = fmt::format("agent {} sca insert_scan_info {}|{}|{}|{}|{}|{}|{}|{}|{}|{}",
                            ctx.agentID,
                            pmStartScan,
                            pmEndScan,
                            scanID,
                            policyID,
                            pass,
                            failed,
                            invalid,
                            totalChecks,
                            score,
                            hash);
    }

    const auto [queryResult, discartPayload] = ctx.wdb->tryQueryAndParseResult(query);

    if (wazuhdb::QueryResultCodes::OK != queryResult)
    {
        WAZUH_LOG_WARN("[SCA] Error saving scan info for agent '{}'", ctx.agentID);
        return false;
    }

    return true;
}

void insertPolicyInfo(const DecodeCxt& ctx)
{
    // "Retrieving sha256 hash for policy id: policy_id"
    const auto query =
        fmt::format("agent {} sca insert_policy {}|{}|{}|{}|{}|{}",
                    ctx.agentID,
                    ctx.getSrcStr(field::Name::NAME).value_or("NULL"),
                    ctx.getSrcStr(field::Name::FILE).value_or("NULL"),
                    ctx.getSrcStr(field::Name::POLICY_ID).value_or("NULL"),
                    ctx.getSrcStr(field::Name::DESCRIPTION).value_or("NULL"),
                    ctx.getSrcStr(field::Name::REFERENCES).value_or("NULL"),
                    ctx.getSrcStr(field::Name::HASH_FILE).value_or("NULL"));

    const auto [result, payload] = ctx.wdb->tryQueryAndParseResult(query);

    if (wazuhdb::QueryResultCodes::OK != result)
    {
        WAZUH_LOG_WARN("Error saving policy info for agent '{}'", ctx.agentID);
    }
    return;
}

void updatePolicyInfo(const DecodeCxt& ctx, const std::string& policyId)
{
    // findPolicySHA256
    const auto query =
        fmt::format("agent {} sca query_policy_sha256 {}", ctx.agentID, policyId);

    const auto [resQuery, oldHashFile] = searchAndParse(query, ctx.wdb);

    switch (resQuery)
    {
        case SearchResult::FOUND:
        {
            const auto eventHashFile = ctx.getSrcStr(field::Name::HASH_FILE).value();

            if (oldHashFile != eventHashFile)
            {
                if (deletePolicyAndCheck(ctx, policyId))
                {
                    pushDumpRequest(ctx, policyId, true);
                }
            }
            else
            {
                WAZUH_LOG_DEBUG("[SCA] Hash file is the same for policy '%s'", policyId);
            }
            break;
        }
        case SearchResult::NOT_FOUND: break;
        case SearchResult::ERROR:
        default:
        {
            WAZUH_LOG_WARN("[SCA] Error querying policy SHA256 database for agent: {}",
                           ctx.agentID);
        }
    }
}

void checkResultsAndDump(const DecodeCxt& ctx,
                         const std::string& policyId,
                         bool isFirstScan,
                         const std::string& eventHash)
{
    bool doPushDumpRequest = false;
    const auto [resQuery, oldEventHash] = findCheckResults(ctx, policyId);

    switch (resQuery)
    {
        case SearchResult::FOUND:
            /* Integrity check */
            if (oldEventHash != eventHash)
            {
                doPushDumpRequest = true;
                WAZUH_LOG_DEBUG(
                    "[SCA] Scan result integrity failed for policy '%s'. Hash from "
                    "DB: '%s', hash from summary: '%s'. Requesting DB dump.",
                    policyId,
                    oldEventHash,
                    eventHash);
            }

            break;
        case SearchResult::NOT_FOUND:
            /* Empty DB */
            doPushDumpRequest = true;
            WAZUH_LOG_DEBUG(
                "[SCA] Check results DB empty for policy '%s'. Requesting DB dump.",
                policyId);
            break;
        default:
            WAZUH_LOG_WARN("[SCA] Error querying check results database for agent: {}",
                           ctx.agentID);
            break;
    }

    if (doPushDumpRequest)
    {
        pushDumpRequest(ctx, policyId, isFirstScan);
    }

    return;
}

bool deletePolicyAndCheck(const DecodeCxt& ctx, const std::string& policyId)
{
    // "Deleting policy '%s', agent id '%s'"
    auto query = fmt::format("agent {} sca delete_policy {}", ctx.agentID, policyId);

    const auto [resDelPolicy, dummyPayload] = ctx.wdb->tryQueryAndParseResult(query);

    if (wazuhdb::QueryResultCodes::OK != resDelPolicy)
    {
        WAZUH_LOG_WARN(
            "[SCA] Error deleting policy '{}' for agent '{}'.", policyId, ctx.agentID);
        return false;
    }

    // "Deleting check for policy '%s', agent id '%s'"
    query = fmt::format("agent {} sca delete_check {}", ctx.agentID, policyId);

    const auto [delCheckResultCode, delCheckPayload] =
        ctx.wdb->tryQueryAndParseResult(query);

    if (wazuhdb::QueryResultCodes::OK != delCheckResultCode)
    {
        WAZUH_LOG_WARN("[SCA] Error deleting check for policy '{}' for agent '{}'.",
                       policyId,
                       ctx.agentID);
        // return false;
    }

    return true;
}

std::tuple<SearchResult, std::string> findCheckResults(const DecodeCxt& ctx,
                                                       const std::string& pID)
{
    // "Find check results for policy id: %s"
    const auto query = fmt::format("agent {} sca query_results {}", ctx.agentID, pID);

    return searchAndParse(query, ctx.wdb);
}

void FillScanInfo(const DecodeCxt& ctx)
{
    ctx.event->setString("summary", ctx.destinationPath.at(field::Name::TYPE));

    // The /name field is renamed to /policy
    ctx.event->set(ctx.destinationPath.at(field::Name::POLICY),
                   ctx.sourcePath.at(field::Name::NAME));

    // Copy if exists
    field::copyIfExist(ctx, field::Name::SCAN_ID);
    field::copyIfExist(ctx, field::Name::DESCRIPTION);
    field::copyIfExist(ctx, field::Name::POLICY_ID);
    field::copyIfExist(ctx, field::Name::PASSED);
    field::copyIfExist(ctx, field::Name::FAILED);
    field::copyIfExist(ctx, field::Name::INVALID);
    field::copyIfExist(ctx, field::Name::TOTAL_CHECKS);
    field::copyIfExist(ctx, field::Name::SCORE);
    field::copyIfExist(ctx, field::Name::FILE);
}

std::optional<std::string> handleScanInfo(const DecodeCxt& ctx)
{
    // Validate the JSON ScanInfo Event
    if (!isValidScanInfoEvent(ctx))
    {
        return "fail on isValidScanInfoEvent"; // Fail on check
    }

    // Get basic info from Event
    const auto policyId = ctx.getSrcStr(field::Name::POLICY_ID).value();
    const auto eventHash = ctx.getSrcStr(field::Name::HASH).value();
    const bool isFirstScan = ctx.existsSrc(field::Name::FIRST_SCAN);

    // Check de policy in the DB
    bool normalize = false;
    bool scanInfoUpdate = false;
    const auto scanInfoQuery =
        fmt::format("agent {} sca query_scan {}", ctx.agentID, policyId);
    const auto [resScanInfo, scanInfo] = searchAndParse(scanInfoQuery, ctx.wdb);

    switch (resScanInfo)
    {
        case SearchResult::FOUND:
        {
            scanInfoUpdate = true;
            // If query fails or hash is not found, storedHash is empty
            const auto storedHash = utils::string::split(scanInfo, ' ').at(0);
            const bool diferentHash = (storedHash != eventHash);
            const bool newHash = (diferentHash && !isFirstScan);

            const bool force_alert = ctx.existsSrc(field::Name::FORCE_ALERT);

            normalize = (newHash || force_alert);

            break;
        }
        case SearchResult::NOT_FOUND:
        {
            scanInfoUpdate = false;
            normalize = true;
            // It not exists, insert
            break;
        }
        case SearchResult::ERROR:
        default:
            WAZUH_LOG_WARN("[SCA] Error querying scan database for agent: {}",
                           ctx.agentID);
            break;
    }

    // Saves sacan info
    if (SearchResult::ERROR != resScanInfo && SaveScanInfo(ctx, scanInfoUpdate))
    {
        if (normalize)
        {
            FillScanInfo(ctx);
        }

        if (!scanInfoUpdate && isFirstScan)
        {
            pushDumpRequest(ctx, policyId, isFirstScan);
        }
    }

    // "Find policies IDs for policy '%s', agent id '%s'"
    const auto policyQuery =
        fmt::format("agent {} sca query_policy {}", ctx.agentID, policyId);

    const auto [resPolQuery, dummyPayload] = searchAndParse(policyQuery, ctx.wdb, false);

    switch (resPolQuery)
    {
        case SearchResult::FOUND:
            // If exists, then sync
            updatePolicyInfo(ctx, policyId);
            break;

        case SearchResult::NOT_FOUND:
            // If not exists, then insert
            insertPolicyInfo(ctx);
            break;

        case SearchResult::ERROR:
        default:
            WAZUH_LOG_WARN(
                "[SCA] Error querying policy monitoring database for agent: {}",
                ctx.agentID);
    }

    // Check and dump!
    checkResultsAndDump(ctx, policyId, isFirstScan, eventHash);

    return {};
}

/****************************************************************************************
                                END HANDLE SCAN INFO
                                     POLICIES
*****************************************************************************************/
std::optional<std::string> handlePoliciesInfo(const DecodeCxt& ctx)
{
    std::optional<std::string> retval {std::nullopt};

    // Check policies JSON
    if (!field::isValidEvent(ctx, {{field::Name::POLICIES, field::Type::ARRAY, true}}))
    {
        return "Error: policies array not found";
    }

    const auto policiesEvent = ctx.getSrcArray(field::Name::POLICIES).value();
    if (policiesEvent.empty())
    {
        WAZUH_LOG_DEBUG("[SCA] No policies found for agent: {}", ctx.agentID);
    }
    else
    {
        // "Retrieving policies from database."
        const auto policiesIdQuery =
            fmt::format("agent {} sca query_policies ", ctx.agentID);
        const auto [resPoliciesIds, policiesDB] =
            searchAndParse(policiesIdQuery, ctx.wdb);

        if (SearchResult::ERROR == resPoliciesIds)
        {
            WAZUH_LOG_WARN("[SCA] Error retrieving policies from database for agent: {}",
                           ctx.agentID);
        }
        else
        {
            /* For each policy id, look if we have scanned it */
            const auto& policiesList = utils::string::split(policiesDB, ',');

            for (auto& pId : policiesList)
            {
                /* This policy is not being scanned anymore, delete it */
                if (std::find_if(policiesEvent.begin(),
                                 policiesEvent.end(),
                                 [&](const auto& policy)
                                 {
                                     auto pStr = policy.getString();
                                     return pStr && pStr.value() == pId;
                                 })
                    == policiesEvent.end())
                {
                    WAZUH_LOG_DEBUG("Policy id doesn't exist: '{}'. Deleting it.", pId);
                    deletePolicyAndCheck(ctx, pId);
                }
            }
        }
    }

    return std::nullopt;
}

/****************************************************************************************
                                END POLICIES
                                    DUMP
*****************************************************************************************/

std::tuple<std::optional<std::string>, std::string, int>
isValidDumpEvent(const DecodeCxt& ctx)
{

    // ScanInfo conditions list
    const std::vector<field::conditionToCheck> conditions = {
        {field::Name::ELEMENTS_SENT, field::Type::INT, true},
        {field::Name::POLICY_ID, field::Type::STRING, true},
        {field::Name::SCAN_ID, field::Type::INT, true},
    };

    if (!field::isValidEvent(ctx, conditions))
    {
        return {"Malformed JSON", "", -1};
    }

    const auto policyId = ctx.getSrcStr(field::Name::POLICY_ID).value();
    const auto scanId = ctx.getSrcInt(field::Name::SCAN_ID).value();

    return {std::nullopt, std::move(policyId), scanId};
}

void deletePolicyCheckDistinct(const DecodeCxt& ctx,
                               const std::string& policyId,
                               const int scanId)
{
    // "Deleting check distinct policy id , agent id "
    const auto query = fmt::format(
        "agent {} sca delete_check_distinct {}|{}", ctx.agentID, policyId, scanId);

    const auto [resultCode, payload] = ctx.wdb->tryQueryAndParseResult(query);
    if (wazuhdb::QueryResultCodes::OK != resultCode)
    {
        WAZUH_LOG_WARN("[SCA] Error deleting check distinct policy id: {} agent id: {}",
                       policyId,
                       ctx.agentID);
    }

    return;
}

// - Dump Handling - //

std::optional<std::string> handleDumpEvent(const DecodeCxt& ctx)
{

    // Check dump event JSON fields
    const auto [checkError, policyId, scanId] = isValidDumpEvent(ctx);

    if (checkError)
    {
        return checkError;
    }

    // "Deleting check distinct policy id , agent id "
    // Continue always, if rare error log error
    deletePolicyCheckDistinct(ctx, policyId, scanId);

    // Retreive hash from db
    const auto [resCheckResult, hashCheckResults] = findCheckResults(ctx, policyId);

    if (SearchResult::FOUND == resCheckResult)
    {
        // Retreive hash from summary
        const auto hashScanQuery =
            fmt::format("agent {} sca query_scan {}", ctx.agentID, policyId);
        const auto [resScanInfo, hashScanInfo] = searchAndParse(hashScanQuery, ctx.wdb);

        if (SearchResult::FOUND == resScanInfo)
        {
            if (hashCheckResults != hashScanInfo)
            {
                pushDumpRequest(ctx, policyId, false);
                WAZUH_LOG_DEBUG(
                    "[SCA] Scan result integrity failed for policy '{}'. Hash from DB: "
                    "'{}' hash from summary: '{}'. Requesting DB dump.",
                    policyId,
                    hashCheckResults,
                    hashScanInfo);
            }
        }
        else if (SearchResult::ERROR == resScanInfo)
        {
            WAZUH_LOG_WARN("[SCA] Error querying summary for policy: {} agent: {}",
                           policyId,
                           ctx.agentID);
        }
    }
    else if (SearchResult::ERROR == resCheckResult)
    {
        WAZUH_LOG_WARN("[SCA] Error querying check results for policy: {} agent: {}",
                       policyId,
                       ctx.agentID);
    }

    return std::nullopt; // Success
}

} // namespace sca

// - Helper - //

base::Expression opBuilderSCAdecoder(const std::any& definition)
{
    // Extract parameters from any
    auto [targetField, name, raw_parameters] =
        helper::base::extractDefinition(definition);
    // Identify references and build JSON pointer paths
    const auto parameters {helper::base::processParameters(raw_parameters)};
    // Assert expected number of parameters
    helper::base::checkParametersSize(parameters, 2);
    // Parameter type check
    helper::base::checkParameterType(parameters[0],
                                     helper::base::Parameter::Type::REFERENCE);
    helper::base::checkParameterType(parameters[1],
                                     helper::base::Parameter::Type::REFERENCE);

    // Format name for the tracer
    name = helper::base::formatHelperFilterName(name, targetField, parameters);

    // Tracing
    const auto successTrace {fmt::format("[{}] -> Success", name)};

    const auto failureTrace1 {
        fmt::format("[{}] -> Failure: [{}] not found", name, parameters[0].m_value)};
    const auto failureTrace2 {
        fmt::format("[{}] -> Failure: [{}] is empty or is not an string",
                    name,
                    parameters[0].m_value + "/type")};
    const auto failureTrace3 {fmt::format(
        "[{}] -> Failure: [{}] unknown type", name, parameters[0].m_value + "/type")};

    /* Create the context for SCA decoder */
    namespace SF = sca::field;
    auto wdb = std::make_shared<wazuhdb::WazuhDB>(wazuhdb::WDB_SOCK_PATH);
    auto cfg = std::make_shared<base::utils::socketInterface::unixDatagram>(wazuhdb::CFG_AR_SOCK_PATH);
    /*  Maps of paths. Contains the orginal path and the mapped path for each field */
    std::unordered_map<SF::Name, std::string> fieldSource {};
    std::unordered_map<SF::Name, std::string> fieldDest {};

    for (SF::Name field = SF::Name::A_BEGIN; field != SF::Name::A_END; ++field)
    {
        fieldSource.insert({field, parameters[0].m_value + SF::getRealtivePath(field)});
        fieldDest.insert({field, std::string {"/sca"} + SF::getRealtivePath(field)});
    }

    // Return Term
    return base::Term<base::EngineOp>::create(
        name,
        [=,
         targetField = std::move(targetField),
         sourceSCApath = parameters[0].m_value,
         agentIdPath = parameters[1].m_value,
         fieldSrc = std::move(fieldSource),
         fieldDst = std::move(fieldDest),
         cfg = std::move(cfg),
         wdb = std::move(wdb)](base::Event event) -> base::result::Result<base::Event>
        {
            std::optional<std::string> error;

            // TODO: this should be checked in the decoder
            if (event->exists(sourceSCApath) && event->exists(agentIdPath)
                && event->isString(agentIdPath))
            {
                const auto agentId = event->getString(agentIdPath).value();
                const auto cxt =
                    sca::DecodeCxt {event, agentId, wdb, cfg, fieldSrc, fieldDst};

                // TODO: Field type is mandatory and should be checked in the decoder
                auto type = event->getString(sourceSCApath + "/type");
                if (!type)
                {
                    // TODO: Change trace message
                    error = failureTrace1;
                }
                // Proccess event with the appropriate handler
                else if (sca::TYPE_CHECK == type.value())
                {
                    error = sca::handleCheckEvent(cxt);
                }
                else if (sca::TYPE_SUMMARY == type.value())
                {
                    error = sca::handleScanInfo(cxt);
                }
                else if (sca::TYPE_POLICIES == type.value())
                {
                    error = sca::handlePoliciesInfo(cxt);
                }
                else if (sca::TYPE_DUMP_END == type.value())
                {
                    error = sca::handleDumpEvent(cxt);
                }
                else
                {
                    // Unknown type value
                    error = failureTrace3;
                }
            }
            else
            {
                error = failureTrace1;
            }

            // Event is processed, return base::Result accordingly
            // Error is nullopt if no error occurred, otherwise it contains the error
            // message
            if (error)
            {
                event->setBool(false, targetField);
                return base::result::makeFailure(event, error.value());
            }
            else
            {
                event->setBool(true, targetField);
                return base::result::makeSuccess(event, successTrace);
            }
        });
}

} // namespace builder::internals::builders
