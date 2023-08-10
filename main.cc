#include "seastar/core/posix.hh"
#include "seastar/core/print.hh"
#include "seastar/core/smp.hh"
#include "seastar/core/thread.hh"
#include "seastar/util/defer.hh"
#include "seastar/util/noncopyable_function.hh"
#include "wasmtime/store.h"

#include <seastar/core/app-template.hh>
#include <seastar/core/coroutine.hh>
#include <seastar/core/future-util.hh>
#include <seastar/core/reactor.hh>
#include <seastar/core/sharded.hh>
#include <seastar/core/sleep.hh>
#include <seastar/core/sstring.hh>
#include <seastar/util/log.hh>

#include <wasm.h>
#include <wasmtime.h>

#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <thread>

namespace ss {
using namespace seastar;
}
static ss::logger lg("log");

namespace {

void exit_with_error(
  std::string_view message, wasmtime_error_t* error, wasm_trap_t* trap) {
    wasm_byte_vec_t error_message;
    if (error != NULL) {
        wasmtime_error_message(error, &error_message);
        wasmtime_error_delete(error);
    } else {
        wasm_trap_message(trap, &error_message);
        wasm_trap_delete(trap);
    }
    std::string err_msg = ss::format(
      "error: {}\n{}",
      message,
      std::string_view(error_message.data, error_message.size));
    wasm_byte_vec_delete(&error_message);
    throw std::runtime_error(err_msg);
}

struct deferred_cleanup {
    void defer(ss::noncopyable_function<void()> func) {
        _cleanup.push_back(ss::defer(std::move(func)));
    }

    std::vector<ss::deferred_action<ss::noncopyable_function<void()>>> _cleanup;
};

void run_wasmtime() {
    deferred_cleanup cleanup;
    // Set up our compilation context. Note that we could also work with a
    // `wasm_config_t` here to configure what feature are enabled and various
    // compilation settings.
    printf("Initializing...\n");
    wasm_engine_t* engine = wasm_engine_new();
    assert(engine != nullptr);
    cleanup.defer([engine] { wasm_engine_delete(engine); });

    // With an engine we can create a *store* which is a long-lived group of
    // wasm modules. Note that we allocate some custom data here to live in the
    // store, but here we skip that and specify nullptr.
    wasmtime_store_t* store = wasmtime_store_new(engine, nullptr, nullptr);
    assert(store != nullptr);
    cleanup.defer([store] { wasmtime_store_delete(store); });
    wasmtime_context_t* context = wasmtime_store_context(store);

    std::string_view raw_wat = R"WAT(
      (module
        (func $boom unreachable)
        (export "boom" (func $boom))
      )
    )WAT";
    wasm_byte_vec_t wat;
    wasm_byte_vec_new(&wat, raw_wat.size(), raw_wat.data());
    cleanup.defer([&wat] { wasm_byte_vec_delete(&wat); });

    // Parse the wat into the binary wasm format
    wasm_byte_vec_t wasm;
    wasmtime_error_t* error = wasmtime_wat2wasm(wat.data, wat.size, &wasm);
    if (error != nullptr) {
        exit_with_error("failed to parse wat", error, nullptr);
    }
    cleanup.defer([&wasm] { wasm_byte_vec_delete(&wasm); });

    // Now that we've got our binary webassembly we can compile our module.
    printf("Compiling module...\n");
    wasmtime_module_t* mod = nullptr;
    error = wasmtime_module_new(engine, (uint8_t*)wasm.data, wasm.size, &mod);
    if (error != nullptr) {
        exit_with_error("failed to compile module", error, nullptr);
    }
    cleanup.defer([mod] { wasmtime_module_delete(mod); });

    // With our callback function we can now instantiate the compiled module,
    // giving us an instance we can then execute exports from. Note that
    // instantiation can trap due to execution of the `start` function, so we
    // need to handle that here too.
    printf("Instantiating module...\n");
    wasm_trap_t* trap = nullptr;
    wasmtime_instance_t instance;
    error = wasmtime_instance_new(context, mod, nullptr, 0, &instance, &trap);
    if (error != nullptr || trap != nullptr) {
        exit_with_error("failed to instantiate", error, trap);
    }

    // Lookup our `boom` export function
    printf("Extracting export...\n");
    wasmtime_extern_t run;
    constexpr std::string_view export_name = "boom";
    bool ok = wasmtime_instance_export_get(
      context, &instance, export_name.data(), export_name.size(), &run);
    assert(ok);
    assert(run.kind == WASMTIME_EXTERN_FUNC);

    // And call it!
    for (int i = 0; i < 1; ++i) {
        printf("Calling export...\n");
        error = wasmtime_func_call(
          context, &run.of.func, nullptr, 0, nullptr, 0, &trap);
        if (error != nullptr) {
            exit_with_error("expected trap", error, trap);
        }
        assert(trap != nullptr);
    }

    printf("All finished!\n");
}

} // namespace

int main(int argc, char** argv) {
    ss::app_template::seastar_options ss_opts;
    // You need at least 2 CPUs for this program.
    ss_opts.smp_opts.smp.set_default_value(2);
    ss::app_template app(std::move(ss_opts));

    return app.run(
      argc, argv, seastar::coroutine::lambda([&]() -> ss::future<> {
          // Try to run wasmtime on 2 different cores and the second run will
          // crash due to SIGILL
          co_await ss::smp::submit_to(0, [] { run_wasmtime(); });
          co_await ss::smp::submit_to(1, [] { run_wasmtime(); });
      }));
}
