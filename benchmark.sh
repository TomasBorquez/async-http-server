echo "GET http://localhost:8080/users" | vegeta attack -duration=30s -rate=100000 > results.bin
# echo "GET http://localhost:8080/"      | vegeta attack -duration=5s -rate=10000 > results.bin
cat results.bin | vegeta plot > plot.html
