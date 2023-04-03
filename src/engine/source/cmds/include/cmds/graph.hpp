#ifndef _CMD_GRAPH_HPP
#define _CMD_GRAPH_HPP

#include <memory>
#include <string>

#include <CLI/CLI.hpp>

#include <metrics/iMetricsManager.hpp>

namespace cmd::graph
{
/**
 * @brief Load and build environment to generate environment graph and environment
 * expression graph.
 *
 * @param kvdbPath Path to KVDB folder.
 * @param fileStorage Path to asset folders.
 * @param environment Name of the environment to be loaded.
 * @param graphOutDir Directory where the graphs will be saved.
 */
struct Options
{
    std::string kvdbPath;
    std::string fileStorage;
    std::string environment;
    std::string graphOutDir;
};
void run(const Options& options, const std::shared_ptr<metricsManager::IMetricsManager>& metricsManager);

void configure(CLI::App_p app, const std::shared_ptr<metricsManager::IMetricsManager>& metricsManager);
} // namespace cmd::graph

#endif // _CMD_GRAPH_HPP