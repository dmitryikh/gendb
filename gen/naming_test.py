import pytest
import naming

def test_split_namespace_class():
    assert naming.split_namespace_class('foo.Bar') == ('foo', 'Bar')
    assert naming.split_namespace_class('foo.bar.Baz') == ('foo.bar', 'Baz')
    assert naming.split_namespace_class('Baz') == ('', 'Baz')
    assert naming.split_namespace_class('foo') == ('', 'foo')


def test_to_cpp_namespace():
    assert naming.to_cpp_namespace('foo.bar') == 'foo::bar'
    assert naming.to_cpp_namespace('foo') == 'foo'
    assert naming.to_cpp_namespace('') == ''


def test_PascalCase_basic():
    assert naming.PascalCase('foo_bar') == 'FooBar'
    assert naming.PascalCase('foo') == 'Foo'
    assert naming.PascalCase('foo_bar_baz') == 'FooBarBaz'
    assert naming.PascalCase('foo1_bar2') == 'Foo1Bar2'
    assert naming.PascalCase('fooBar') == 'Foobar'  # Not camelCase aware
    assert naming.PascalCase('FooBar') == 'FooBar'  # Already PascalCase
    assert naming.PascalCase('Foo') == 'Foo'        # Already PascalCase
    assert naming.PascalCase('') == ''


def test_PascalCase_idempotent():
    assert naming.PascalCase('FooBar') == 'FooBar'
    assert naming.PascalCase('Foo') == 'Foo'


def test_snake_case_basic():
    assert naming.snake_case('FooBar') == 'foo_bar'
    assert naming.snake_case('FooBarBaz') == 'foo_bar_baz'
    assert naming.snake_case('Foo') == 'foo'
    assert naming.snake_case('Foo1Bar2') == 'foo1_bar2'
    assert naming.snake_case('foo_bar') == 'foo_bar'  # Already snake_case
    assert naming.snake_case('foo') == 'foo'          # Already snake_case
    assert naming.snake_case('foo_bar_baz') == 'foo_bar_baz'  # Already snake_case
    assert naming.snake_case('') == ''


def test_snake_case_idempotent():
    assert naming.snake_case('foo_bar') == 'foo_bar'
    assert naming.snake_case('foo') == 'foo'
    assert naming.snake_case('foo_bar_baz') == 'foo_bar_baz'


def test_snake_case_edge_cases():
    # Mixed case, numbers, and underscores
    assert naming.snake_case('FooBarBaz1') == 'foo_bar_baz1'
    assert naming.snake_case('FooBarBaz_Extra') == 'foo_bar_baz_extra'
    assert naming.snake_case('FooBarBazExtra1') == 'foo_bar_baz_extra1'
    assert naming.snake_case('FOOBar') == 'foo_bar'  # Only first capital treated as word
    assert naming.snake_case('fooBarBaz') == 'foo_bar_baz'
    assert naming.snake_case('fooBar') == 'foo_bar'
    assert naming.snake_case('fooBarBazQux') == 'foo_bar_baz_qux'

if __name__ == '__main__':
    pytest.main()
