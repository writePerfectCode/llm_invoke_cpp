#include <iostream>

#include <json_invoke/json_invoke.hpp>
#include <tools/trace_recorder.hpp>

namespace {

void printResponse(const std::string& label, const json_invoke::json& response)
{
    std::cout << "\n[response] " << label << std::endl;
    std::cout << response.dump(2) << std::endl;
}

void printTrace(const json_invoke::VectorTraceRecorder& recorder)
{
    std::cout << "[trace recorder]" << std::endl;
    std::cout << recorder.toJson().dump(2) << std::endl;
}

void invokeAndPrint(
    json_invoke::JsonInvokeAdapter& adapter,
    json_invoke::VectorTraceRecorder& recorder,
    const std::string& label,
    json_invoke::json request)
{
    recorder.clear();
    const auto response = adapter.invokeJson(std::move(request));
    printResponse(label, response);
    printTrace(recorder);
}

} // namespace

int main()
{
    json_invoke::JsonInvokeAdapter adapter;
    json_invoke::VectorTraceRecorder recorder;

    adapter.setTraceSink(recorder.sink());
    adapter.registerFunction(
        "sum",
        json_invoke::readOnly([](int left, int right) { return left + right; }),
        json_invoke::FunctionMetadata{{"left", "right"}, "Add two integers."});

    invokeAndPrint(
        adapter,
        recorder,
        "sum",
        {
            {"name", "sum"},
            {"args", {{"left", 2}, {"right", 5}}},
        });

    invokeAndPrint(
        adapter,
        recorder,
        "sum missing argument",
        {
            {"name", "sum"},
            {"args", {{"left", 2}}},
        });

    return 0;
}