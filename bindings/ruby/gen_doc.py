import os
import sys

BASE = os.path.join(os.path.dirname(__file__), '../..')

sys.path.append(os.path.join(BASE, 'bindings'))

import generator
from generator import LaTeX

def generate_func(x):
    assert x.form in (generator.AsyncCall, generator.Iterator)
    func = 'Client :: %s(' % x.name
    padd = ' ' * 16
    func += ', '.join([str(arg).lower()[18:-2] for arg in x.args_in])
    func += ')\n'
    return func

ARGS_IN = {(generator.AsyncCall, generator.SpaceName): 'The name of the space as a string or symbol.'
          ,(generator.AsyncCall, generator.Key): 'The key for the operation as a Ruby type'
          ,(generator.AsyncCall, generator.Attributes): 'A hash specifying attributes '
           'to modify and their respective values.'
          ,(generator.AsyncCall, generator.MapAttributes): 'A hash specifying map '
           'attributes to modify and their respective key/values.'
          ,(generator.AsyncCall, generator.Predicates): 'A hash of predicates '
           'to check against.'
          ,(generator.Iterator, generator.SpaceName): 'The name of the space as string or symbol.'
          ,(generator.Iterator, generator.SortBy): 'The attribute to sort by.'
          ,(generator.Iterator, generator.Limit): 'The number of results to return.'
          ,(generator.Iterator, generator.MaxMin): 'Maximize (!= 0) or minimize (== 0).'
          ,(generator.Iterator, generator.Predicates): 'A hash of predicates '
           'to check against.'
          }

def generate_doc_block(x):
    output = ''
    output += '\\paragraph{{\code{{{0}}}}}\n'.format(LaTeX(c.name))
    output += '\\label{{api:ruby:{0}}}\n'.format(c.name)
    output += '\\index{{{0}!Ruby API}}\n'.format(LaTeX(c.name))
    output += '\\begin{ccode}\n'
    output += generate_func(c)
    output += '\\end{ccode}\n'
    output += '\\funcdesc \input{{\\topdir/api/desc/{0}}}\n\n'.format(c.name)
    output += '\\noindent\\textbf{Parameters:}\n'
    max_label = ''
    parameters = ''
    for arg in c.args_in:
        if (c.form, arg) not in ARGS_IN:
            print 'missing in', (c.form, arg)
            continue
        label = '\\code{' + LaTeX(str(arg).lower()[18:-2]) + '}'
        parameters += '\\item[{0}] {1}\n'.format(label, ARGS_IN[(c.form, arg)])
        if len(label) > len(max_label):
            max_label = label
    if parameters:
        output += '\\begin{description}[labelindent=\\widthof{{' + max_label + '}},leftmargin=*,noitemsep,nolistsep,align=right]\n'
        output += parameters
        output += '\\end{description}\n'
    else:
        output += 'None\n'
    output += '\n\\noindent\\textbf{Returns:}\n'
    if c.args_out == (generator.Status,):
        if generator.Predicates in c.args_in:
            output += 'True if predicate, False if not predicate.  Raises exception on error.'
        else:
            output += 'True.  Raises exception on error.'
    elif c.args_out == (generator.Status, generator.Attributes):
        output += 'Object if found, nil if not found.  Raises exception on error.'
    elif c.args_out == (generator.Status, generator.Count):
        output += 'Number of objects found.  Raises exception on error.'
    elif c.args_out == (generator.Status, generator.Description):
        output += 'Description of search.  Raises exception on error.'
    else:
        assert False
    output += '\n\n'
    return output

if __name__ == '__main__':
    with open(os.path.join(BASE, 'doc/api/ruby.client.tex'), 'w') as fout:
        output = '% This LaTeX file is generated by bindings/ruby/gen_doc.py\n\n'
        for c in generator.Client:
            output += generate_doc_block(c)
        output = output.strip()
        fout.write(output + '\n')
