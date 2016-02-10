$(document).ready(function() {

  function getTemplate(templateName) {
    return document.querySelector('#' + templateName + '-template').innerHTML;
  }

  if ($('body').hasClass('faq')) {

    var search = instantsearch({
      // Replace with your own values
      appId: '2BTFITEL0N',
      apiKey: 'f27772a74737b4bb66eb0999104830d2',
      indexName: 'infinit_sh_faq',
      urlSync: true
    });

    var widgets = [
      instantsearch.widgets.searchBox({
        container: '#search-input',
        placeholder: 'Search a question...'
      }),

      instantsearch.widgets.hits({
        container: '#hits',
        hitsPerPage: 20,
        templates: {
          item: getTemplate('hit'),
          empty: getTemplate('no-results')
        }
      }),

      // instantsearch.widgets.stats({
      //   container: '#stats'
      // }),

      // instantsearch.widgets.pagination({
      //   container: '#pagination'
      // }),

      instantsearch.widgets.menu({
        container: '#category',
        attributeName: 'category',
        limit: 10,
        operator: 'or',
        cssClasses: {
          active: 'active'
        }
      })

      // instantsearch.widgets.clearAll({
      //   container: '#clear',
      //   templates: {
      //     link: 'Popular'
      //   },
      //   autoHideContainer: false
      // })
    ];

    widgets.forEach(search.addWidget, search);

    search.start();
  }

});