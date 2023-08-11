// Copyright 2023 Northern.tech AS
//
//    Licensed under the Apache License, Version 2.0 (the "License");
//    you may not use this file except in compliance with the License.
//    You may obtain a copy of the License at
//
//        http://www.apache.org/licenses/LICENSE-2.0
//
//    Unless required by applicable law or agreed to in writing, software
//    distributed under the License is distributed on an "AS IS" BASIS,
//    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
//    See the License for the specific language governing permissions and
//    limitations under the License.

#include <string>
#include <vector>

#include <common/conf.hpp>
#include <common/http.hpp>
#include <common/log.hpp>
#include <common/setup.hpp>
#include <common/expected.hpp>

#include <mender-auth/ipc/server.hpp>
#include <mender-auth/actions.hpp>

using namespace std;

namespace conf = mender::common::conf;
namespace error = mender::common::error;
namespace events = mender::common::events;
namespace http = mender::http;
namespace ipc = mender::auth::ipc;
namespace mlog = mender::common::log;
namespace setup = mender::common::setup;
namespace expected = mender::common::expected;
namespace io = mender::common::io;
namespace actions = mender::auth::actions;

static expected::ExpectedString GetPassphraseFromFile(const string &filepath) {
	string passphrase = "";
	if (filepath == "") {
		return passphrase;
	}

	auto ex_ifs = io::OpenIfstream(filepath == "-" ? io::paths::Stdin : filepath);
	if (!ex_ifs) {
		return expected::unexpected(ex_ifs.error());
	}
	auto &ifs = ex_ifs.value();

	errno = 0;
	getline(ifs, passphrase);
	if (ifs.bad()) {
		int io_errno = errno;
		error::Error err {
			generic_category().default_error_condition(io_errno),
			"Failed to read passphrase from '" + filepath + "'"};
		return expected::unexpected(err);
	}

	return passphrase;
}

static actions::ExpectedActionPtr ParseUpdateArguments(
	vector<string>::const_iterator start, vector<string>::const_iterator end) {
	if (start == end) {
		return expected::unexpected(conf::MakeError(conf::InvalidOptionsError, "Need an action"));
	}

	if (start[0] == "daemon") {
		string passphrase = "";
		conf::CmdlineOptionsIterator opts_iter(start + 1, end, {"--passphrase-file"}, {});
		auto ex_opt_val = opts_iter.Next();

		while (ex_opt_val
			   && ((ex_opt_val.value().option != "") || (ex_opt_val.value().value != ""))) {
			auto opt_val = ex_opt_val.value();
			if ((opt_val.option == "--passphrase-file")) {
				auto ex_passphrase = GetPassphraseFromFile(opt_val.value);
				if (!ex_passphrase) {
					return expected::unexpected(ex_passphrase.error());
				}
				passphrase = ex_passphrase.value();
			}
			ex_opt_val = opts_iter.Next();
		}
		if (!ex_opt_val) {
			return expected::unexpected(ex_opt_val.error());
		}
		return actions::DaemonAction::Create(passphrase);
	} else {
		return expected::unexpected(
			conf::MakeError(conf::InvalidOptionsError, "No such action: " + start[0]));
	}
}

error::Error DoMain(int argc, char *argv[]) {
	setup::GlobalSetup();

	conf::MenderConfig config;
	if (argc > 1) {
		vector<string> args(argv + 1, argv + argc);
		auto arg_pos = config.ProcessCmdlineArgs(args.begin(), args.end());
		if (!arg_pos) {
			return arg_pos.error();
		}

		auto action = ParseUpdateArguments(args.begin() + arg_pos.value(), args.end());
		if (!action) {
			return action.error();
		}
	} else {
		auto err = config.LoadDefaults();
		if (err != error::NoError) {
			return err;
		}
	}

	events::EventLoop loop {};

	auto ipc_server {ipc::Server(loop, config)};

	const string server_url {"http://127.0.0.1:8001"};

	auto err = ipc_server.Listen(server_url);
	if (err != error::NoError) {
		mlog::Error("Failed to start the listen loop");
		mlog::Error(err.String());
		return error::MakeError(error::ExitWithFailureError, "");
	}

	loop.Run();
	mlog::Info("Finished running the main loop!");

	return error::NoError;
}

int main(int argc, char *argv[]) {
	auto err = DoMain(argc, argv);

	if (err != error::NoError) {
		if (err.code == error::MakeError(error::ExitWithSuccessError, "").code) {
			return 0;
		} else if (err.code != error::MakeError(error::ExitWithFailureError, "").code) {
			cerr << "Failed to process command line options: " + err.String() << endl;
		}
		return 1;
	}

	return 0;
}
