// Copyright 2013-present Facebook. All Rights Reserved.

#include <pbxsetting/pbxsetting.h>
#include <xcsdk/xcsdk.h>
#include <pbxproj/pbxproj.h>
#include <pbxspec/pbxspec.h>
#include <xcscheme/xcscheme.h>
#include <xcworkspace/xcworkspace.h>
#include <pbxbuild/pbxbuild.h>

using libutil::FSUtil;

static std::vector<pbxproj::PBX::BuildPhase::shared_ptr>
SortBuildPhases(std::map<pbxproj::PBX::BuildPhase::shared_ptr, std::vector<pbxbuild::ToolInvocation>> phaseInvocations)
{
    std::unordered_map<std::string, pbxproj::PBX::BuildPhase::shared_ptr> outputToPhase;
    for (auto const &entry : phaseInvocations) {
        for (pbxbuild::ToolInvocation const &invocation : entry.second) {
            for (std::string const &output : invocation.outputs()) {
                outputToPhase.insert({ output, entry.first });
            }
        }
    }

    pbxbuild::BuildGraph<pbxproj::PBX::BuildPhase::shared_ptr> phaseGraph;
    for (auto const &entry : phaseInvocations) {
        phaseGraph.insert(entry.first, { });

        for (pbxbuild::ToolInvocation const &invocation : entry.second) {
            for (std::string const &input : invocation.inputs()) {
                auto it = outputToPhase.find(input);
                if (it != outputToPhase.end()) {
                    if (it->second != entry.first) {
                        phaseGraph.insert(entry.first, { it->second });
                    }
                }
            }
        }
    }

    return phaseGraph.ordered();
}

static std::vector<pbxbuild::ToolInvocation>
SortInvocations(std::vector<pbxbuild::ToolInvocation> invocations)
{
    std::unordered_map<std::string, pbxbuild::ToolInvocation const *> outputToInvocation;
    for (pbxbuild::ToolInvocation const &invocation : invocations) {
        for (std::string const &output : invocation.outputs()) {
            outputToInvocation.insert({ output, &invocation });
        }
    }

    pbxbuild::BuildGraph<pbxbuild::ToolInvocation const *> graph;
    for (pbxbuild::ToolInvocation const &invocation : invocations) {
        graph.insert(&invocation, { });

        for (std::string const &input : invocation.inputs()) {
            auto it = outputToInvocation.find(input);
            if (it != outputToInvocation.end()) {
                graph.insert(&invocation, { it->second });
            }
        }
    }

    std::vector<pbxbuild::ToolInvocation> result;
    for (pbxbuild::ToolInvocation const *invocation : graph.ordered()) {
        result.push_back(*invocation);
    }
    return result;
}

static void
PerformBuild(pbxbuild::BuildEnvironment const &buildEnvironment, pbxbuild::BuildContext const &buildContext, pbxproj::PBX::Target::shared_ptr const &target, pbxbuild::TargetEnvironment const &targetEnvironment, std::map<pbxproj::PBX::BuildPhase::shared_ptr, std::vector<pbxbuild::ToolInvocation>> const &toolInvocations, std::vector<pbxproj::PBX::BuildPhase::shared_ptr> const &orderedPhases)
{
    printf("=== BUILD TARGET %s OF PROJECT %s WITH CONFIGURATION %s ===\n\n", target->name().c_str(), target->project()->name().c_str(), buildContext.configuration().c_str());
    printf("Check dependencies\n\n");

    printf("Write auxiliary files\n");
    for (pbxproj::PBX::BuildPhase::shared_ptr const &buildPhase : orderedPhases) {
        auto const entry = toolInvocations.find(buildPhase);
        for (pbxbuild::ToolInvocation const &invocation : entry->second) {
            for (std::string const &output : invocation.outputs()) {
                std::string directory = FSUtil::GetDirectoryName(output);
                if (!FSUtil::TestForDirectory(directory)) {
                    printf("/bin/mkdir -p %s\n", directory.c_str());
                    // TODO(grp): Create the directory.
                }
            }

            for (pbxbuild::ToolInvocation::AuxiliaryFile const &auxiliaryFile : invocation.auxiliaryFiles()) {
                if (!FSUtil::TestForRead(auxiliaryFile.path())) {
                    printf("write-file %s\n", auxiliaryFile.path().c_str());
                    // TODO(grp): Write the response file out.

                    if (auxiliaryFile.executable() && !FSUtil::TestForExecute(auxiliaryFile.path())) {
                        printf("chmod 0755 %s\n", auxiliaryFile.path().c_str());
                        // TODO(grp): Make the script executable.
                    }
                }
            }
        }
    }
    printf("\n");

    printf("Create product structure\n");
    // TODO(grp): Create the product structure.
    printf("\n");

    for (pbxproj::PBX::BuildPhase::shared_ptr const &buildPhase : orderedPhases) {
        auto const entry = toolInvocations.find(buildPhase);
        std::vector<pbxbuild::ToolInvocation> orderedInvocations = SortInvocations(entry->second);

        for (pbxbuild::ToolInvocation const &invocation : orderedInvocations) {
            printf("%s\n", invocation.logMessage().c_str());

            printf("\tcd %s\n", invocation.workingDirectory().c_str());
            // TODO(grp): Change into this directory.

            for (std::pair<std::string, std::string> const &entry : invocation.environment()) {
                printf("\texport %s=%s\n", entry.first.c_str(), entry.second.c_str());
            }

            std::string executable = invocation.executable();
            if (!FSUtil::IsAbsolutePath(executable)) {
                executable = FSUtil::FindExecutable(executable, targetEnvironment.sdk()->executablePaths());
            }
            printf("\t%s", executable.c_str());

            for (std::string const &arg : invocation.arguments()) {
                printf(" %s", arg.c_str());
            }
            printf("\n");
            // TODO(grp): Invoke command.

            printf("\tInputs:\n");
            for (std::string const &input : invocation.inputs()) {
                printf("\t\t%s\n", input.c_str());
            }
            printf("\tOutputs:\n");
            for (std::string const &output : invocation.outputs()) {
                printf("\t\t%s\n", output.c_str());
            }
            if (!invocation.auxiliaryFiles().empty()) {
                printf("\tAuxiliaries:\n");
                for (pbxbuild::ToolInvocation::AuxiliaryFile const &auxiliaryFile : invocation.auxiliaryFiles()) {
                    printf("\t\t%s\n", auxiliaryFile.path().c_str());
                }
            }

            printf("\n");
        }
    }
}

static void
BuildTarget(pbxbuild::BuildEnvironment const &buildEnvironment, pbxbuild::BuildContext const &buildContext, pbxproj::PBX::Target::shared_ptr const &target)
{
    std::unique_ptr<pbxbuild::TargetEnvironment> targetEnvironmentPtr = buildContext.targetEnvironment(buildEnvironment, target);
    if (targetEnvironmentPtr == nullptr) {
        fprintf(stderr, "error: couldn't create target environment\n");
        return;
    }
    pbxbuild::TargetEnvironment targetEnvironment = *targetEnvironmentPtr;

    // Filter build phases to ones appropriate for this target.
    std::vector<pbxproj::PBX::BuildPhase::shared_ptr> buildPhases;
    for (pbxproj::PBX::BuildPhase::shared_ptr const &buildPhase : target->buildPhases()) {
        // TODO(grp): Check buildActionMask against buildContext.action.
        // TODO(grp): Check runOnlyForDeploymentPostprocessing.
        buildPhases.push_back(buildPhase);
    }

    pbxbuild::Phase::PhaseContext phaseContext = pbxbuild::Phase::PhaseContext(buildEnvironment, buildContext, targetEnvironment);

    std::map<pbxproj::PBX::BuildPhase::shared_ptr, std::vector<pbxbuild::ToolInvocation>> toolInvocations;

    std::map<std::pair<std::string, std::string>, std::vector<pbxbuild::ToolInvocation>> sourcesInvocations;
    for (pbxproj::PBX::BuildPhase::shared_ptr const &buildPhase : buildPhases) {
        if (buildPhase->type() == pbxproj::PBX::BuildPhase::kTypeSources) {
            auto BP = std::static_pointer_cast <pbxproj::PBX::SourcesBuildPhase> (buildPhase);
            auto sources = pbxbuild::Phase::SourcesResolver::Create(phaseContext, BP);
            if (sources != nullptr) {
                sourcesInvocations.insert(sources->variantArchitectureInvocations().begin(), sources->variantArchitectureInvocations().end());
                toolInvocations.insert({ buildPhase, sources->invocations() });
            }
        }
    }

    for (pbxproj::PBX::BuildPhase::shared_ptr const &buildPhase : buildPhases) {
        switch (buildPhase->type()) {
            case pbxproj::PBX::BuildPhase::kTypeFrameworks: {
                auto BP = std::static_pointer_cast <pbxproj::PBX::FrameworksBuildPhase> (buildPhase);
                auto link = pbxbuild::Phase::FrameworksResolver::Create(phaseContext, BP, sourcesInvocations);
                if (link != nullptr) {
                    toolInvocations.insert({ buildPhase, link->invocations() });
                }
                break;
            }
            case pbxproj::PBX::BuildPhase::kTypeShellScript: {
                auto BP = std::static_pointer_cast <pbxproj::PBX::ShellScriptBuildPhase> (buildPhase);
                auto shellScript = pbxbuild::Phase::ShellScriptResolver::Create(phaseContext, BP);
                if (shellScript != nullptr) {
                    toolInvocations.insert({ buildPhase, shellScript->invocations() });
                }
                break;
            }
            default: break;
        }
    }


    for (pbxproj::PBX::BuildPhase::shared_ptr const &buildPhase : buildPhases) {
        switch (buildPhase->type()) {
            case pbxproj::PBX::BuildPhase::kTypeShellScript:
            case pbxproj::PBX::BuildPhase::kTypeFrameworks:
            case pbxproj::PBX::BuildPhase::kTypeSources: {
                break;
            }
            case pbxproj::PBX::BuildPhase::kTypeHeaders: {
                // TODO: Copy Headers
                auto BP = std::static_pointer_cast <pbxproj::PBX::HeadersBuildPhase> (buildPhase);
                break;
            }
            case pbxproj::PBX::BuildPhase::kTypeResources: {
                // TODO: Copy Resources
                auto BP = std::static_pointer_cast <pbxproj::PBX::ResourcesBuildPhase> (buildPhase);
                break;
            }
            case pbxproj::PBX::BuildPhase::kTypeCopyFiles: {
                // TODO: Copy Files
                auto BP = std::static_pointer_cast <pbxproj::PBX::CopyFilesBuildPhase> (buildPhase);
                break;
            }
            case pbxproj::PBX::BuildPhase::kTypeAppleScript: {
                // TODO: Compile AppleScript
                auto BP = std::static_pointer_cast <pbxproj::PBX::AppleScriptBuildPhase> (buildPhase);
                break;
            }
        }
    }

    std::vector<pbxproj::PBX::BuildPhase::shared_ptr> orderedPhases = SortBuildPhases(toolInvocations);

    PerformBuild(buildEnvironment, buildContext, target, targetEnvironment, toolInvocations, orderedPhases);
}

int
main(int argc, char **argv)
{
    std::unique_ptr<pbxbuild::BuildEnvironment> buildEnvironment = pbxbuild::BuildEnvironment::Default();
    if (buildEnvironment == nullptr) {
        fprintf(stderr, "error: couldn't create build environment\n");
        return -1;
    }

    if (argc < 5) {
        printf("Usage: %s workspace scheme config action\n", argv[0]);
        return -1;
    }

    xcworkspace::XC::Workspace::shared_ptr workspace = xcworkspace::XC::Workspace::Open(argv[1]);
    if (workspace == nullptr) {
        fprintf(stderr, "failed opening workspace\n");
        return -1;
    }

    xcscheme::SchemeGroup::shared_ptr group = xcscheme::SchemeGroup::Open(workspace->projectFile(), workspace->name());
    if (group == nullptr) {
        fprintf(stderr, "failed opening scheme\n");
        return -1;
    }

    xcscheme::XC::Scheme::shared_ptr scheme = nullptr;
    for (xcscheme::XC::Scheme::shared_ptr const &available : group->schemes()) {
        printf("scheme: %s\n", available->name().c_str());
        if (available->name() == argv[2]) {
            scheme = available;
            break;
        }
    }
    if (scheme == nullptr) {
        fprintf(stderr, "couldn't find scheme\n");
        return -1;
    }

    pbxbuild::BuildContext context = pbxbuild::BuildContext::Workspace(
        workspace,
        scheme,
        argv[3],
        argv[4]
    );

    pbxbuild::DependencyResolver resolver = pbxbuild::DependencyResolver(*buildEnvironment);
    auto graph = resolver.resolveDependencies(context);
    std::vector<pbxproj::PBX::Target::shared_ptr> targets = graph.ordered();

    for (pbxproj::PBX::Target::shared_ptr const &target : targets) {
        BuildTarget(*buildEnvironment, context, target);
    }
}
