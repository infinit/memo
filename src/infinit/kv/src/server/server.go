package server

import (
	dht "doughnut"
	"fmt"
	"github.com/golang/protobuf/proto"
	"golang.org/x/net/context"
	"google.golang.org/grpc"
	"google.golang.org/grpc/codes"
	"google.golang.org/grpc/grpclog"
	data "kv/data"
	service "kv/service"
	"sort"
	"strings"
)

type kvServer struct {
	vStore      dht.DoughnutClient
	name        string
	rootAddress []byte
}

func NewServer(vStore dht.DoughnutClient, name string, bootstrap bool) *kvServer {
	s := new(kvServer)
	s.vStore = vStore
	s.name = name
	nbAddr, err := vStore.NamedBlockAddress(context.Background(), &dht.NamedBlockAddressRequest{Key: []byte(name)})
	if err != nil {
		grpclog.Fatalf("unable to get named block address: %v\n", err)
	}
	if nb, err := vStore.Fetch(context.Background(), &dht.FetchRequest{Address: nbAddr.Address}); err == nil {
    // Fetch root block.
    s.rootAddress = nb.GetBlock().GetData()
    if _, err := s.rootBlock(); err != nil {
      grpclog.Fatalf("unable to fetch root block: %v\n", err)
    }
  } else { // Named block does not exist.
		if bootstrap == false {
			grpclog.Fatalf("unable to find named block: %v\n", err)
		}
		// Create root block.
		rb, err := vStore.MakeMutableBlock(context.Background(), &dht.MakeMutableBlockRequest{})
		if err != nil {
			grpclog.Fatalf("unable to create root block: %v\n", err)
		}
		nb, err := vStore.MakeNamedBlock(context.Background(), &dht.MakeNamedBlockRequest{Key: []byte(name)})
		if err != nil {
			grpclog.Fatalf("unable to create named block: %v\n", err)
		}
		nb.Payload = &dht.Block_Data{rb.Address}
		if _, err = vStore.Insert(context.Background(), &dht.InsertRequest{Block: nb}); err != nil {
			grpclog.Fatalf("unable to insert named block: %v\n", err)
		}
		if _, err = vStore.Insert(context.Background(), &dht.InsertRequest{Block: rb}); err != nil {
			grpclog.Fatalf("unable to insert root block: %v\n", err)
		}
		s.rootAddress = rb.Address
	}
	return s
}

func (server *kvServer) put(key string, value []byte, update bool, atomic bool) error {
	if key == "" {
		return grpc.Errorf(codes.InvalidArgument, "key is empty")
	}
	if value == nil {
		return grpc.Errorf(codes.InvalidArgument, "value is nil")
	}
	db, err := server.vStore.MakeImmutableBlock(context.Background(), &dht.MakeImmutableBlockRequest{Data: value})
	if err != nil {
		return grpc.Errorf(codes.Internal, "unable to make immutable block: %v", err)
	}
	store := data.ValueStore{Address: db.Address}
	var opType operationType
	if update == true {
		opType = UPDATE
	} else if atomic == true {
		opType = INSERT
	} else {
		opType = UPSERT
	}
	op := operation{opType: opType, key: key, store: store, commit: false}
	if err = server.store(op); err != nil {
		if _, ok := err.(AlreadyExistsError); ok {
			return grpc.Errorf(codes.AlreadyExists, err.Error())
		} else if _, ok := err.(DoesNotExistError); ok {
			return grpc.Errorf(codes.NotFound, err.Error())
		}
		return grpc.Errorf(codes.Internal, "unable to store edit: %v", err)
	}
	if _, err := server.vStore.Insert(context.Background(), &dht.InsertRequest{Block: db}); err != nil {
		server.store(operation{opType: CLEANUP_EDIT, key: key, store: store})
		return grpc.Errorf(codes.Internal, "unable to store data block: %v", err)
	}
	op.commit = true
	if err = server.store(op); err != nil {
		server.store(operation{opType: CLEANUP_EDIT, key: key, store: store})
		if _, ok := err.(AlreadyExistsError); ok {
			return grpc.Errorf(codes.AlreadyExists, err.Error())
		} else if _, ok := err.(DoesNotExistError); ok {
			return grpc.Errorf(codes.NotFound, err.Error())
		}
		return grpc.Errorf(codes.Internal, "unable to store commit: %v", err)
	}
	return nil
}

func (server *kvServer) Insert(ctx context.Context, req *service.InsertRequest) (*service.InsertResponse, error) {
	if err := server.put(req.GetKey(), req.GetValue(), false, true); err != nil {
		return nil, err
	}
	return &service.InsertResponse{}, nil
}

func (server *kvServer) Update(ctx context.Context, req *service.UpdateRequest) (*service.UpdateResponse, error) {
	if err := server.put(req.GetKey(), req.GetValue(), true, true); err != nil {
		return nil, err
	}
	return &service.UpdateResponse{}, nil
}

func (server *kvServer) Upsert(ctx context.Context, req *service.UpsertRequest) (*service.UpsertResponse, error) {
	if err := server.put(req.GetKey(), req.GetValue(), false, false); err != nil {
		return nil, err
	}
	return &service.UpsertResponse{}, nil
}

func (server *kvServer) Get(ctx context.Context, req *service.GetRequest) (*service.GetResponse, error) {
	key := req.GetKey()
	if key == "" {
		return nil, grpc.Errorf(codes.InvalidArgument, "key is empty")
	}
	block, err := server.rootBlock()
	if err != nil {
		return nil, err
	}
	km, err := getMap(block)
	if err != nil {
		return nil, err
	}
	desc := km.GetMap()[key]
	if desc == nil || desc.GetCurrent() == nil || desc.GetCurrent().GetAddress() == nil {
		return nil, grpc.Errorf(codes.NotFound, "no value for key: %v", key)
	}
	chb, err := server.vStore.Fetch(context.Background(), &dht.FetchRequest{Address: desc.GetCurrent().GetAddress()})
	if err != nil {
		return nil, grpc.Errorf(codes.Internal, "unable to fetch value: %v", err)
	}
	return &service.GetResponse{Value: chb.GetBlock().GetData()}, nil
}

func (server *kvServer) Remove(ctx context.Context, req *service.RemoveRequest) (*service.RemoveResponse, error) {
	key := req.GetKey()
	if key == "" {
		return nil, grpc.Errorf(codes.InvalidArgument, "key is empty")
	}
	op := operation{opType: DELETE, key: key}
	if err := server.store(op); err != nil {
		if _, ok := err.(DoesNotExistError); ok {
			return nil, grpc.Errorf(codes.NotFound, err.Error())
		}
		return nil, err
	}
	return &service.RemoveResponse{}, nil
}

func (server *kvServer) List(ctx context.Context, req *service.ListRequest) (*service.ListResponse, error) {
	block, err := server.rootBlock()
	if err != nil {
		return nil, err
	}
	km, err := getMap(block)
	if err != nil {
		return nil, err
	}
	keys := []string{}
	for k := range km.GetMap() {
		keys = append(keys, k)
	}
	sort.Strings(keys)
	items := []*service.ListItem{}
	prefixes := []string{}
	truncated := false
	markerFound := false
	for i, k := range keys {
		if !strings.HasPrefix(k, req.GetPrefix()) {
			continue
		}
		if req.GetMarker() != "" && !markerFound {
			markerFound = strings.HasSuffix(k, req.GetMarker())
			continue
		}
		if req.GetDelimiter() != "" {
			startAt := len(req.GetPrefix())
			delimPos := strings.Index(k[startAt:], req.GetDelimiter())
			if delimPos > 0 {
				prefix := k[startAt : len(req.GetPrefix())+delimPos+1]
				newPrefix := true
				for _, p := range prefixes {
					if p == prefix {
						newPrefix = false
						break
					}
				}
				if newPrefix == true {
					prefixes = append(prefixes, prefix)
				}
			}
		}
		items = append(items, &service.ListItem{Key: k})
		if req.GetMaxKeys() > 0 && uint64(len(items)) == req.GetMaxKeys() {
			truncated = i != len(keys)-1 && strings.HasPrefix(keys[i+1], req.GetPrefix())
			break
		}
	}
	return &service.ListResponse{Items: items, Prefixes: prefixes, Truncated: truncated}, nil
}

func (server *kvServer) rootBlock() (*dht.Block, error) {
	rb, err := server.vStore.Fetch(context.Background(), &dht.FetchRequest{Address: server.rootAddress, DecryptData: true})
	if err != nil {
		return nil, grpc.Errorf(codes.Internal, "unable to fetch root block: %v", err)
	}
	return rb.GetBlock(), nil
}

type AlreadyExistsError struct {
	Key string
}

func (e AlreadyExistsError) Error() string {
	return fmt.Sprintf("key already exists: %s", e.Key)
}

type DoesNotExistError struct {
	Key string
}

func (e DoesNotExistError) Error() string {
	return fmt.Sprintf("key does not exist: %s", e.Key)
}

type operationType uint8

const (
	INSERT operationType = iota + 1
	UPDATE
	UPSERT
	DELETE
	CLEANUP_EDIT
)

type operation struct {
	opType operationType
	key    string
	store  data.ValueStore
	commit bool
}

func (server *kvServer) store(op operation) error {
	block, err := server.rootBlock()
	if err != nil {
		return err
	}
	km, err := getMap(block)
	if err != nil {
		return err
	}
	for {
		switch op.opType {
		default:
			return nil
		case INSERT:
			fallthrough
		case UPDATE:
			fallthrough
		case UPSERT:
			if desc, ok := km.GetMap()[op.key]; ok {
				if desc.GetCurrent() != nil {
					if op.opType == INSERT {
						return AlreadyExistsError{op.key}
					}
				} else if op.opType == UPDATE {
					return DoesNotExistError{op.key}
				}
			} else if op.opType == UPDATE {
				return DoesNotExistError{op.key}
			}
			if op.commit == true {
				if desc, ok := km.GetMap()[op.key]; ok {
					if desc.GetCurrent().GetAddress() != nil {
						server.vStore.Remove(context.Background(), &dht.RemoveRequest{Address: desc.GetCurrent().GetAddress()})
					}
					desc.Current = &op.store
					editIndex := -1
					for i, e := range desc.GetEdits() {
						if addressEqual(e.GetAddress(), op.store.GetAddress()) {
							editIndex = i
							break
						}
					}
					if editIndex != -1 {
						desc.Edits = append(desc.Edits[:editIndex], desc.Edits[editIndex+1:]...)
					}
				} else {
					return grpc.Errorf(codes.Internal, "no edit for commit: %s", op.key)
				}
			} else {
				if desc, ok := km.GetMap()[op.key]; ok {
					if desc.GetEdits() == nil {
						desc.Edits = []*data.ValueStore{&op.store}
					} else {
						desc.Edits = append(desc.Edits, &op.store)
					}
				} else {
					desc := data.ValueDescriptor{Edits: []*data.ValueStore{&op.store}}
					km.Map[op.key] = &desc
				}
			}
		case DELETE:
			if desc, ok := km.GetMap()[op.key]; ok {
				store := desc.GetCurrent()
				if store != nil && store.GetAddress() != nil {
					server.vStore.Remove(context.Background(), &dht.RemoveRequest{Address: store.GetAddress()})
					desc.Current.Address = nil
					if len(desc.GetEdits()) == 0 {
						delete(km.Map, op.key)
					}
				} else {
					return DoesNotExistError{op.key}
				}
			} else {
				return DoesNotExistError{op.key}
			}
		case CLEANUP_EDIT:
			if desc, ok := km.GetMap()[op.key]; ok {
				editIndex := -1
				for i, e := range desc.GetEdits() {
					if addressEqual(e.GetAddress(), op.store.GetAddress()) {
						editIndex = i
						break
					}
				}
				if editIndex != -1 {
					desc.Edits = append(desc.Edits[:editIndex], desc.Edits[editIndex+1:]...)
				}
			}
		}
		payload, err := proto.Marshal(km)
		if err != nil {
			return err
		}
		block.Payload = &dht.Block_DataPlain{payload}
		status, err := server.vStore.Update(context.Background(), &dht.UpdateRequest{Block: block, DecryptData: true})
		if err != nil {
			return err
		}
		if status.GetCurrent() == nil {
			break
		}
		block = status.Current
		if km, err = getMap(block); err != nil {
			return err
		}
	}
	return nil
}

func getMap(block *dht.Block) (*data.KeyMap, error) {
	res := &data.KeyMap{}
	err := proto.Unmarshal(block.GetDataPlain(), res)
	if err != nil {
		return nil, grpc.Errorf(codes.Internal, "unable to get map: %v", err)
	}
	if res.GetMap() == nil {
		res.Map = map[string]*data.ValueDescriptor{}
	}
	return res, nil
}

func addressEqual(lhs []byte, rhs []byte) bool {
	if len(lhs) != len(rhs) {
		return false
	}
	for i, b := range lhs {
		if rhs[i] != b {
			return false
		}
	}
	return true
}
