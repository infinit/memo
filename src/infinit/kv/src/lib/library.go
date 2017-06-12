package main

import (
	"C"
	"doughnut"
	"google.golang.org/grpc"
	"google.golang.org/grpc/grpclog"
	kv "kvs/service"
	"net"
	"server"
)

var grpcServer *grpc.Server

//export RunServer
func RunServer(name string, vStoreEnpoint string, grpcEndpoint string, bootstrap bool, grpcPort *int) {
	if grpcServer != nil {
		grpclog.Printf("gRPC server already running")
		return
	}
	vConn, err := grpc.Dial(vStoreEnpoint, grpc.WithInsecure())
	if err != nil {
		grpclog.Printf("error connecting to value store: %v", err)
		return
	}
	defer vConn.Close()
	client := doughnut.NewDoughnutClient(vConn)

	kvConn, err := net.Listen("tcp", grpcEndpoint)
	if err != nil {
		grpclog.Printf("error listening on %s: %v", grpcEndpoint, err)
		return
	}
	defer kvConn.Close()
	*grpcPort = kvConn.Addr().(*net.TCPAddr).Port

	grpcServer = grpc.NewServer()
	kv.RegisterKeyValueStoreServer(grpcServer, server.NewServer(client, name, bootstrap))
	grpcServer.Serve(kvConn)
}

//export StopServer
func StopServer() {
	if grpcServer == nil {
		grpclog.Printf("gRPC server not running")
		return
	}
	grpclog.Printf("signaled, stopping...")
	grpcServer.Stop()
}

func main() {
}
