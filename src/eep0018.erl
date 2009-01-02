-module(eep0018).

-export([start/0, start/1]).
-export([stop/0]).

-export([json_to_term/1, json_to_term/2]).
-export([term_to_json/1]).

%% constants (see eep0018.h)

-define(JSON_PARSE,     1).
-define(JSON_PARSE_EI,  2).

-define(ATOM,             10).
-define(NUMBER,           11).
-define(STRING,           12).
-define(KEY,              13).
-define(MAP,              14).
-define(ARRAY,            15).
-define(END,              16).
-define(EI,               17).

%% start/stop port

start() ->
  start(".").
 
start(LibPath) ->
  case erl_ddll:load_driver(LibPath, "eep0018_drv") of
    ok -> ok;
    {error, already_loaded} -> ok;
    {error, X} -> exit({error, X});
    _ -> exit({error, could_not_load_driver})
  end,
  spawn(fun() -> init("eep0018_drv") end).

init(SharedLib) ->
  register(eep0018, self()),
  Port = open_port({spawn, SharedLib}, []),
  loop(Port).

stop() ->
  eep0018 ! stop.

%% logging helpers. 

l(X) ->
  io:format("Log: ~p ~n", [ X ]), X.

l(M, X) ->
  io:format(M ++ ": ~p ~n", [ X ]), X.


%
% Options are as follows
%
% {float,true}
% {label,binary|atom|existing_atom}
% {duplicate_labels,true/false/raise} % raise badarg
%

-record(options, {list_to_label, list_to_number, duplicate_labels, strict_order}).

fetch_option(Property, In, Default) ->
  Default.

build_options(In) ->
  #options{
    list_to_label   = list_to_label_fun(fetch_option(labels, In, binary)),
    list_to_number  = list_to_number_fun(fetch_option(float, In, true)),
    duplicate_labels= duplicate_labels_fun(fetch_option(duplicate_labels, In, true))
  }.

%% Convert labels

list_to_label_fun(binary)         ->  fun(S) -> S end;
list_to_label_fun(atom)           ->  
  fun(S) -> 
    try list_to_atom(S) of A -> A 
    catch _:_ -> S end
  end;
list_to_label_fun(existing_atom)  ->  
  fun(S) -> 
    try list_to_existing_atom(S) of A -> A 
    catch _:_ -> S end
  end.

list_to_label(O, S) ->
  (O#options.list_to_label)(S).

%% Convert numbers

list_to_number_fun(false) ->  
  fun(S) ->
    try list_to_integer(S) of
      I -> I
    catch
      _:_ -> list_to_float(S)
    end
  end;

list_to_number_fun(true) ->  
  fun(S) -> list_to_float(S) end.

list_to_number(O, S) ->
  (O#options.list_to_number)(S).

%% Finish maps

duplicate_labels_fun(true)  -> fun(S) -> S end.

% Not yet implemented
% duplicate_labels_fun(false)  
% duplicate_labels_fun(raise)  

%% receive values from the Sax driver %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%

receive_map(O, In) -> 
  case receive_value(O) of
    'end' -> 
      (O#options.duplicate_labels)(
        lists:reverse(In)
      );
    Key   -> receive_map(O, [ {Key, receive_value(O)} | In ])
  end.

receive_array(O, In) ->  
  case receive_value(O) of
    'end' -> lists:reverse(In);
    T     -> receive_array(O, [ T | In ])
  end.

receive_array(O, In, null) -> 
  receive_array(O, In);
receive_array(O, In, Consumer) -> 
  case receive_value(O) of
    'end' -> [];
    T     -> Consumer(T), receive_array(O, In, Consumer)
  end.

receive_ei_encoded(DATA) ->
   erlang:binary_to_term(list_to_binary(DATA)).

receive_value(O) -> 
  receive
    { _, { _, [ ?MAP ] } }    -> receive_map(O, []);
    { _, { _, [ ?ARRAY ] } }  -> receive_array(O, []);
    { _, { _, [ ?END ] } }    -> 'end';

    { _, { _, [ ?ATOM | DATA ] } }   -> list_to_atom(DATA);
    { _, { _, [ ?NUMBER | DATA ] } } -> list_to_number(O, DATA);
    { _, { _, [ ?STRING | DATA ] } } -> list_to_binary(DATA);
    { _, { _, [ ?KEY | DATA ] } }    -> list_to_label(O, DATA);

    { _, { _, [ ?EI | DATA ] } }     -> receive_ei_encoded(DATA);
    % erlang:binary_to_term(DATA);

    UNKNOWN                   -> io:format("UNKNOWN 1 ~p ~n", [UNKNOWN]), UNKNOWN
  end.

receive_toplevel_value(O, Consumer) ->
  receive
    { _, { _, [ ?MAP ] } }        -> receive_map(O, []);
    { _, { _, [ ?ARRAY ] } }      -> receive_array(O, [], Consumer);
    { _, { _, [ ?EI | DATA ] } }  -> receive_ei_encoded(DATA);
    
    UNKNOWN                   -> io:format("UNKNOWN 2 ~p ~n", [UNKNOWN]), UNKNOWN
  end.

loop(Port) ->
  receive
    {parse, Caller, X, O} ->
      % Port ! {self(), {command, [ ?JSON_PARSE | X ]}},
      Port ! {self(), {command, [ ?JSON_PARSE_EI | X ]}},
      
      Toplevel = fun(S) -> Caller ! {object, S} end,
      
      Result = receive_toplevel_value(build_options(O), Toplevel),
      
      Caller ! {result, Result},
      loop(Port);

    stop ->
      Port ! {self(), close},
      receive
        {Port, closed} -> exit(normal)
      end;

    {'EXIT', Port, Reason} ->
      io:format("exit ~p ~n", [Reason]),
      exit(port_terminated);
      
    X ->
      io:format("Unknown input! ~p ~n", [X]),
      loop(Port)
  end.

%% control loop for driver

parse(X,O)  -> 
  eep0018 ! {parse, self(), X, O},
  receive_results().
  
receive_results() ->
  receive
    {object, Object} -> [ Object | receive_results() ];
    {result, Result} -> Result
  end.

% The public, EEP0018-like interface.

%
%
json_to_term(X) -> parse(X, []).
json_to_term(X,O) -> parse(X, O).

% 
term_to_json(X) -> 
  rabbitmq:encode(X).
