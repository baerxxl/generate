;
; export-to-gml.scm
;
; Create some simple networks, and export them to GML - Graph Modelling
; Language, see https://en.wikipedia.org/wiki/Graph_Modelling_Language
; This allows the networks to be visualized with CytoScape or Gephi.
;
; The PNG image `n20.gml.png` is an example random network generated
; with the code below, and visualized with CytoScape.
;
(use-modules (opencog) (opencog generate))
(use-modules (ice-9 textual-ports))

; Load the dataset we plan to demo.
(load "basic-network.scm")

;; Generate a collection of random networks, using the lexis, the
;; connectable end-points and the probability weightings previously
;; defined. The result of running this will generate multiple graphs.
;;
;; Aggregation will be started on "burr-3" -- this is the seed, or
;; root from which the rest of the graph will be constructed.
;;
;; XXX Caution, this is not the final API -- subjet to change.
(define graph-set
	(cog-random-aggregate polarity-set lexis weights (Concept "burr-3")))

;; Convert the Atomese representation of these networks into
;; a GML string.
(define gml-string (export-to-gml graph-set))

;; It appears that CytoScape does not really like files with multiple
;; graphs in them. Thus, we'll pick just one graph, and export that.
(define just-one (Set (gar graph-set)))
(define just-one-gml (export-to-gml just-one))

;; Print the string to a file.
(let ((outport (open-file "/tmp/basic-random-net.gml" "w")))
   (put-string outport just-one-gml)
   (close outport))

;; Hush printing when loading this file.
*unspecified*
