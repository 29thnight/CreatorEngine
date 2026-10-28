export module Creator.Containers.TestConsumer;
export import Creator.Containers;
export ce::utf8_string module_round_trip(ce::utf8_string value){return value;}
export bool module_queue(ce::bounded_mpsc_queue<int,7>&q){return q.try_push(73);}
