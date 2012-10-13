run() {
cmd=$1
lossy=$2
if $lossy; then
    ./start.sh 5;
else
    ./start.sh;
fi

if ($cmd); then
    echo $cmd "Success";
else
    echo $cmd "failed";
    ./stop.sh;
    exit 1;
fi
./stop.sh;
}

islossy=false
run "./test-lab-2-a.pl ./yfs1" $islossy;
run "./test-lab-2-b.pl ./yfs1 ./yfs2" $islossy;
run "./test-lab-3-a.pl ./yfs1" $islossy;
run "./test-lab-3-b ./yfs1 ./yfs2" $islossy;
run "./test-lab-3-c ./yfs1 ./yfs2" $islossy;

islossy=true
run "./test-lab-2-a.pl ./yfs1" $islossy;
run "./test-lab-2-b.pl ./yfs1 ./yfs2" $islossy;
run "./test-lab-3-a.pl ./yfs1" $islossy;
run "./test-lab-3-b ./yfs1 ./yfs2" $islossy;
run "./test-lab-3-c ./yfs1 ./yfs2" $islossy;

