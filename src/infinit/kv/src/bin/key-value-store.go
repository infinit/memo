package main

import (
	"doughnut"
	"flag"
	"google.golang.org/grpc"
	"google.golang.org/grpc/grpclog"
	kv "kv/service"
	"net"
	"server"
)

func main() {
	kvName := flag.String("name", "test", "name of key-value store")
	kvBootstrap := flag.Bool("bootstrap", false, "bootstrap the key-value store")
	vEndpoint := flag.String("value-store", "127.0.0.1:50000", "endpoint of value store to use")
	kvEndpoint := flag.String("grpc", "0.0.0.0:50001", "endpoint to serve key-value store")
	flag.Parse()

	if len(*kvName) == 0 {
		grpclog.Fatalln("no name specified")
	}

	grpclog.Printf("connecting to value store at endpoint %v\n", *vEndpoint)
	vConn, err := grpc.Dial(*vEndpoint, grpc.WithInsecure())
	if err != nil {
		grpclog.Fatalf("unable to connect to value store: %v\n", err)
	}
	defer vConn.Close()
	client := doughnut.NewDoughnutClient(vConn)
	grpclog.Printf("connected to value store")

	grpclog.Printf("starting key-value store at endpoint %v\n", *kvEndpoint)
	kvConn, err := net.Listen("tcp", *kvEndpoint)
	if err != nil {
		grpclog.Fatalf("failed to listen: %v\n", err)
	}
	defer kvConn.Close()
	grpcServer := grpc.NewServer()
	kv.RegisterKvServer(grpcServer, server.NewServer(client, *kvName, *kvBootstrap))
	grpclog.Printf("key-value store ready\n")
	grpcServer.Serve(kvConn)
}
