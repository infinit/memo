#! /bin/sh

peer=$1
storage=false
plugin=false

set -e

if test "--help" =  "$peer"; then
  echo "Usage: infinit-static.sh PEER [enable_storage] [enable_volume_plugin]"
  echo "  enabling storage requires opening the port 51234 in tcp and udp"
  echo "  enabling plugin requires host filesystem mounted on /tmp/hostroot"
  exit 0
fi


#generate deterministic network configuration
cat <<EOF >/tmp/cluster_user
{
    "name" : "cluster_user",
    "private_key" : {
        "rsa" : "MIIEowIBAAKCAQEA6osnD9n0fXQS5+REAWxpCcblE60p/7WPTqKgjpdjPwSAJ8iFGNwZJKpbtrqso7QeEOj4vXTw363G8HkEmhXa4GDMWqRVVVwnUUwjIpN3eEF29WzV5/2ZB2E5tyDvFWetRU3XKe+ACc9TOMTMZ0dXJoHuqYoZJajMsd0ctrpXnJvWB5cwD749Fl3PPRenUxVTDHQX1VsWLdDYfCzkkPXL2VF3WxHlX92QYMnhGClb3hBGe5bP8VrkMKUJyMrv6fhGK907RO1NJ21qmHIg/H9LHk1F5n2RkQEHovk5Z6wI6pOhRQMWf1pLyuDzsHZkbb4sU1+70czK6JUOmBI0hs0SowIDAQABAoIBABjY+x9ryMINrP8SKL453PrjLluiScPEWVVMV1Qj56jCck1Eqg0TLDq9LZAzQJzrNRA3jjqMDAS2ZVAOkhU84X5J4vxrUnsDk0rClSpRkyOTU+X2TMcSD8c/XpzvCUUsQagI8hWIQPlJFJj8CGecoDf9JeqiLb3WnzDsquEU5rk5eUVtXQIesxsJ6aiYZgAAd2Jg81KZxcTgeWQh2WQk3VDsUjHtvF7rBv+9qPsrlz8TeacUQ7omhgIVHUy6dy0YZARMw6KmHa0j1KIO4S1azebCAhmpEBFyqINls3kP/FP7FrPP6FAoVtlHxLZK3srPqVW/AlXMIxfH6fc7C7aNFHkCgYEA/WRyMtRfX2R8YJxUH0oc7L/XpGeYt2oOuBP/3zf6pfRlCvBWaIzJHrgE2+AkoY5kFatm4ND6vQVU17j4k3cfxGpnS1eyyLBpEZjN9vcL6tRglUmWmOwYKFlw+371J2QrZl3U+rfxzH1pLJOX6dSDArdg31dIGfGdnjZFNGGBnn0CgYEA7PUMyQpqb3VMvWdtHn5aPibXmqPPQvnzYJQB5gs1kpmIyrm2yzn5WKs3OOHdeyAeUYjq1/4c0cn3sIAkOEG/prZoQp1WO6JJHyarYd+MG4oMOGxo9DNkUetRMnzPm4HIZzCIQU/Yn7ChQjmbhE67zWpHpJaY3Ta3qQxn06gMn58CgYEAvE8pjxzEw9+pjyKeYaJyXH81grh6hdQLnEFApmKzoyE89iQmEwyNaobXFZA9qNJpDrGSgwDLVi3gH3EXSn/827s3iIZkF0EC1FD6v85YzOuH22oUwRCz40iU7lIrXlrS5gBVhv2sdIu+3aHSA7QqqJofI9t5ec5VlH5Ab+0GpwECgYBhnGyg4IaJ1YNAPrvHpPsdwThtvm8hwv9L2IyTrChsdIzSHgC75OzfZuB/sSNglhGHOuSrB0Xt8cnzzkWdWxBM57U5Q8EDHc4LZA2Tatg3e/2evKHbftQjntE7AAkxoRvhzi9C4FZ3Kfaz5jE3JovciZxro5HjBqhPngmjOgXBNwKBgCazhhDNoOEeNBydjfOe4tHDavyNsdJDj9Ha13qti0KYQK0ax90GHHqYXSODrCaaJOXDk+f+1tTHo354/PqdDYN6TDc7yo3pefKvwv9suCs9KQDjh3FumJJRq6liK++6ZEAhloaCXey9B9089NLuZZBlvAFCpOQ3lBhz+WnXXXWJ"
    },
    "public_key" : {
        "rsa" : "MIIBCgKCAQEA6osnD9n0fXQS5+REAWxpCcblE60p/7WPTqKgjpdjPwSAJ8iFGNwZJKpbtrqso7QeEOj4vXTw363G8HkEmhXa4GDMWqRVVVwnUUwjIpN3eEF29WzV5/2ZB2E5tyDvFWetRU3XKe+ACc9TOMTMZ0dXJoHuqYoZJajMsd0ctrpXnJvWB5cwD749Fl3PPRenUxVTDHQX1VsWLdDYfCzkkPXL2VF3WxHlX92QYMnhGClb3hBGe5bP8VrkMKUJyMrv6fhGK907RO1NJ21qmHIg/H9LHk1F5n2RkQEHovk5Z6wI6pOhRQMWf1pLyuDzsHZkbb4sU1+70czK6JUOmBI0hs0SowIDAQAB"
    }
}
EOF
infinit-user --import -i /tmp/cluster_user

if test "$1" = "enable_storage" || test "$2" = "enable_storage" || test "$3" = "enable_storage"; then
  storage=true
fi

if test "$1" = "enable_volume_plugin" || test "$2" = "enable_volume_plugin" || test "$3" = "enable_volume_plugin"; then
  plugin=true
fi

if $storage; then
  infinit-storage --create --name cluster_storage --filesystem
  infinit-network --create --name cluster_network --kelips --storage cluster_storage --as cluster_user --port 51234
else
  infinit-network --create --name cluster_network --kelips --as cluster_user
fi
infinit-volume --create --name cluster_volume --network cluster_network --as cluster_user

if ! test -z "$peer"; then
  peer="--peer $peer"
fi

infinit-volume --update cluster_user/cluster_volume --as cluster_user \
  --user cluster_user --cache $peer

if $storage; then
  infinit-network --run cluster_network --as cluster_user $peer &
fi
#start infinit-daemon
if $plugin; then
  infinit-daemon --start --foreground \
    --docker-socket-path /tmp/hostroot/run/docker/plugins \
    --docker-descriptor-path /tmp/hostroot/usr/lib/docker/plugins \
    --mount-root /tmp/hostroot/tmp/ \
    --docker-mount-substitute "hostroot/tmp:"
fi

while true; do
  sleep 100
done