import Creator.Containers.TestConsumer;
int main(){
 auto value=module_round_trip(ce::utf8_string("cross named module boundary heap string"));
 if(value!=ce::utf8_string_view("cross named module boundary heap string"))return 1;
 ce::bounded_mpsc_queue<int,7> q;int out=0;
 return !module_queue(q)||!q.try_pop(out)||out!=73;
}
