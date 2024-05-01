# Vendoring

To keep track of the vendored code origin and changes, we're using
git subtrees for vendoring 3rd party code.  The advantage of this
approach (over git submodules) is that with the vendored code is
stored inside the repository, instead of only a link to a specific
commit of the remote repository.

Example usage:
```
# Vendor a new library
git subtree add --prefix deps/github.com/redis/hiredis \
  https://github.com/redis/hiredis.git v1.0.0 --squash

# Pull changes from the remote repository
git subtree pull --prefix deps/github.com/redis/hiredis \
  https://github.com/redis/hiredis.git v1.0.1 --squash

# Push modifications of vendored sources to the remote repository
git subtree push --prefix deps/github.com/redis/hiredis \
  https://github.com/some-one/hiredis.git some-updates
```
