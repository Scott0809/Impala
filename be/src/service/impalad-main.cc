// (c) 2012 Cloudera, Inc. All rights reserved.
//
// This file contains the main() function for the impala daemon process,
// which exports the Thrift services ImpalaService and ImpalaBackendService.

#include <jni.h>
#include <boost/scoped_ptr.hpp>
#include <boost/unordered_map.hpp>
#include <glog/logging.h>
#include <gflags/gflags.h>

#include <protocol/TBinaryProtocol.h>
#include <server/TThreadPoolServer.h>
#include <transport/TServerSocket.h>
#include <server/TServer.h>
#include <transport/TTransportUtils.h>
#include <concurrency/PosixThreadFactory.h>

#include "codegen/llvm-codegen.h"
#include "common/status.h"
#include "runtime/coordinator.h"
#include "runtime/exec-env.h"
#include "testutil/test-exec-env.h"
#include "util/jni-util.h"
#include "service/backend-service.h"
#include "service/impala-service.h"
#include "gen-cpp/ImpalaService.h"
#include "gen-cpp/ImpalaBackendService.h"

using namespace impala;
using namespace std;
using namespace boost;
using namespace apache::thrift::server;
using namespace apache::thrift::protocol;
using namespace apache::thrift::transport;
using namespace apache::thrift::concurrency;

DEFINE_int32(fe_port, 21000, "port on which ImpalaService is exported");
DEFINE_int32(be_port, 22000, "port on which ImpalaBackendService is exported");
DEFINE_string(classpath, "", "java classpath");

namespace impala {

static void RunServer(TServer* server) {
  VLOG(1) << "started backend server thread";
  server->serve();
}

// Start jvm and backend service.
static void StartImpalaService(int port) {
  shared_ptr<TProtocolFactory> protocol_factory(new TBinaryProtocolFactory());
  LOG(INFO) << "ImpalaService trying to listen on " << port;

  shared_ptr<ImpalaService> handler(new ImpalaService(FLAGS_fe_port));
  // this first call to getJNIEnv() (which should be this one) creates a jvm
  JNIEnv* env = getJNIEnv();
  handler->Init(env);
  shared_ptr<TProcessor> processor(new ImpalaServiceProcessor(handler));
  shared_ptr<TServerTransport> server_transport(new TServerSocket(port));
  shared_ptr<TTransportFactory> transport_factory(new TBufferedTransportFactory());
  shared_ptr<ThreadManager> thread_mgr(ThreadManager::newSimpleThreadManager());
  // TODO: do we want a BoostThreadFactory?
  shared_ptr<ThreadFactory> thread_factory(new PosixThreadFactory());
  thread_mgr->threadFactory(thread_factory);
  thread_mgr->start();

  LOG(INFO) << "ImpalaService listening on " << port;
  TThreadPoolServer* server = new TThreadPoolServer(
      processor, server_transport, transport_factory, protocol_factory,
      thread_mgr);
  server->serve();
}

}

int main(int argc, char** argv) {
  google::InitGoogleLogging(argv[0]);
  google::ParseCommandLineFlags(&argc, &argv, true);
  LlvmCodeGen::InitializeLlvm();

  // start backend service for the coordinator on backend_port
  ExecEnv exec_env;
  TServer* be_server = StartImpalaBackendService(&exec_env, FLAGS_be_port);
  thread be_server_thread = thread(&RunServer, be_server);

  // this blocks until the fe server terminates
  StartImpalaService(FLAGS_fe_port);
}
